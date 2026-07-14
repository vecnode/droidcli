#include "tools/mini_http_server.hpp"

#include "tools/sync_http_client.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace droidcli::tools {
namespace {

bool ensure_socket_library()
{
#if defined(_WIN32)
	static bool initialized = false;
	if (!initialized)
	{
		WSADATA wsa_data;
		if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
		{
			return false;
		}
		initialized = true;
	}
#endif
	return true;
}

void close_socket(const SocketHandle socket)
{
#if defined(_WIN32)
	closesocket(socket);
#else
	close(socket);
#endif
}

core::String to_lower_ascii(core::String value)
{
	for (char& character : value)
	{
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}
	return value;
}

core::String trim_ascii(core::String value)
{
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
	{
		value.erase(value.begin());
	}
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
	{
		value.pop_back();
	}
	return value;
}

// Header lookup is case-insensitive per RFC 7230.
core::String find_header_value(const net::HttpRequest& request, const core::String& lower_name)
{
	for (const net::HttpHeader& header : request.headers)
	{
		if (to_lower_ascii(header.name) == lower_name)
		{
			return header.value;
		}
	}
	return {};
}

// A request needs a matching Authorization: Bearer <token> header if it hits
// any /api/* route, or /ai/chat (Ollama calls have a cost even though they
// can't execute shell commands - gated for consistency). /health, /echo, and
// /notify stay open: liveness checks and a trivial log-only notify shouldn't
// require an operator to carry the token around.
bool request_requires_auth(const core::String& path)
{
	return path.rfind("/api/", 0) == 0 || path == "/ai/chat";
}

bool is_authorized(const net::HttpRequest& request, const core::String& api_token)
{
	if (api_token.empty())
	{
		// Should never happen in practice (droidcli.cpp always configures a
		// token), but fail closed rather than silently allowing unauthenticated
		// access to a misconfigured server.
		return false;
	}

	const core::String header_value = find_header_value(request, "authorization");
	const core::String prefix = "Bearer ";
	if (header_value.rfind(prefix, 0) != 0)
	{
		return false;
	}

	return header_value.substr(prefix.size()) == api_token;
}

bool parse_request_line(const core::String& line, net::HttpRequest& out_request)
{
	std::istringstream stream(line);
	core::String method;
	core::String path;
	core::String version;
	stream >> method >> path >> version;
	if (method.empty() || path.empty())
	{
		return false;
	}

	method = to_lower_ascii(method);
	if (method == "get")
	{
		out_request.method = net::HttpMethod::Get;
	}
	else if (method == "post")
	{
		out_request.method = net::HttpMethod::Post;
	}
	else
	{
		out_request.method = net::HttpMethod::Unknown;
	}

	const size_t query_index = path.find('?');
	if (query_index != core::String::npos)
	{
		out_request.query_string = path.substr(query_index + 1);
		path = path.substr(0, query_index);
	}
	out_request.path = path;
	return true;
}

} // namespace

void MiniHttpServer::configure_language_ai(const MiniHttpServerOptions& options)
{
	language_ai_transport_.post_json = [](
		const core::String& url,
		const core::String& body,
		int32_t& status_code_out,
		core::String& response_body_out)
	{
		return sync_http_post_json(url, body, status_code_out, response_body_out);
	};

	if (!options.enable_language_ai)
	{
		language_ai_.set_runtime_enabled(false);
		return;
	}

	language_ai_.set_runtime_enabled(true);
	language_ai_.set_ollama_config(options.ollama_config);
	if (!options.system_prompt.empty())
	{
		language_ai_.set_system_prompt(options.system_prompt);
	}
}

bool MiniHttpServer::read_request(const int client_socket, net::HttpRequest& out_request) const
{
	core::String buffer;
	char chunk[1024];
	int content_length = 0;

	while (buffer.find("\r\n\r\n") == core::String::npos)
	{
		const int received = recv(client_socket, chunk, sizeof(chunk), 0);
		if (received <= 0)
		{
			return false;
		}
		buffer.append(chunk, static_cast<size_t>(received));
		if (buffer.size() > 65536)
		{
			return false;
		}
	}

	const size_t header_end = buffer.find("\r\n\r\n");
	const core::String header_block = buffer.substr(0, header_end);
	const core::String body_prefix = buffer.substr(header_end + 4);

	std::istringstream header_stream(header_block);
	core::String line;
	if (!std::getline(header_stream, line))
	{
		return false;
	}
	if (!line.empty() && line.back() == '\r')
	{
		line.pop_back();
	}
	if (!parse_request_line(line, out_request))
	{
		return false;
	}

	while (std::getline(header_stream, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		const size_t colon = line.find(':');
		if (colon != core::String::npos)
		{
			net::HttpHeader header;
			header.name = trim_ascii(line.substr(0, colon));
			header.value = trim_ascii(line.substr(colon + 1));
			out_request.headers.push_back(header);
		}

		const core::String lower = to_lower_ascii(line);
		if (lower.rfind("content-length:", 0) == 0)
		{
			content_length = std::atoi(trim_ascii(line.substr(15)).c_str());
		}
	}

	out_request.body = body_prefix;
	while (static_cast<int>(out_request.body.size()) < content_length)
	{
		const int received = recv(client_socket, chunk, sizeof(chunk), 0);
		if (received <= 0)
		{
			break;
		}
		out_request.body.append(chunk, static_cast<size_t>(received));
	}
	if (content_length > 0)
	{
		out_request.body = out_request.body.substr(0, static_cast<size_t>(content_length));
	}

	return true;
}

bool MiniHttpServer::write_response(const int client_socket, const net::HttpResponse& response) const
{
	core::String payload = "HTTP/1.1 " + std::to_string(static_cast<int>(response.status)) + " OK\r\n";
	payload += "Content-Type: " + response.content_type + "\r\n";
	payload += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
	payload += "Connection: close\r\n\r\n";
	payload += response.body;

	const char* data = payload.c_str();
	size_t remaining = payload.size();
	while (remaining > 0)
	{
		const int sent = send(client_socket, data, static_cast<int>(remaining), 0);
		if (sent <= 0)
		{
			return false;
		}
		data += sent;
		remaining -= static_cast<size_t>(sent);
	}
	return true;
}

bool MiniHttpServer::start(const MiniHttpServerOptions& options)
{
	stop();
	if (!ensure_socket_library())
	{
		return false;
	}

	options_ = options;
	configure_language_ai(options);
	socket_handle_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
	if (socket_handle_ == static_cast<int>(kInvalidSocket))
	{
		socket_handle_ = -1;
		return false;
	}

	int reuse = 1;
	setsockopt(
		static_cast<SocketHandle>(socket_handle_),
		SOL_SOCKET,
		SO_REUSEADDR,
		reinterpret_cast<const char*>(&reuse),
		sizeof(reuse));

	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(static_cast<uint16_t>(options.port));

	if (bind(static_cast<SocketHandle>(socket_handle_), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
	{
		stop();
		return false;
	}

	if (listen(static_cast<SocketHandle>(socket_handle_), 8) != 0)
	{
		stop();
		return false;
	}

	return true;
}

void MiniHttpServer::stop()
{
	if (socket_handle_ >= 0)
	{
		close_socket(static_cast<SocketHandle>(socket_handle_));
		socket_handle_ = -1;
	}
}

bool MiniHttpServer::poll_once(const int32_t timeout_ms)
{
	if (socket_handle_ < 0)
	{
		return false;
	}

	fd_set read_set;
	FD_ZERO(&read_set);
	FD_SET(static_cast<SocketHandle>(socket_handle_), &read_set);

	timeval timeout {};
	timeout.tv_sec = timeout_ms / 1000;
	timeout.tv_usec = (timeout_ms % 1000) * 1000;

	const SocketHandle max_socket = static_cast<SocketHandle>(socket_handle_);
	if (select(static_cast<int>(max_socket + 1), &read_set, nullptr, nullptr, &timeout) <= 0)
	{
		return true;
	}

	sockaddr_in client_address {};
	socklen_t client_length = sizeof(client_address);
	const SocketHandle client_socket = accept(
		static_cast<SocketHandle>(socket_handle_),
		reinterpret_cast<sockaddr*>(&client_address),
		&client_length);
	if (client_socket == kInvalidSocket)
	{
		return true;
	}

	net::HttpRequest request;
	if (read_request(static_cast<int>(client_socket), request))
	{
		net::HttpResponse response;
		if (request_requires_auth(request.path) && !is_authorized(request, options_.api_token))
		{
			response.status = net::HttpStatus::Unauthorized;
			response.body = "{\"error\":\"unauthorized\",\"message\":"
				"\"missing or invalid Authorization: Bearer <token> header\"}";
			write_response(static_cast<int>(client_socket), response);
			close_socket(client_socket);
			return true;
		}

		net::HandlerContext context;
		context.session = options_.session;
		if (options_.enable_language_ai)
		{
			context.language_ai = &language_ai_;
			context.language_ai_transport = &language_ai_transport_;
		}
		const net::RouteDispatchResult dispatch = routes_.dispatch(request, context);
		if (dispatch.handled)
		{
			response = dispatch.response;
			if (dispatch.notify.has_notify_message && options_.on_notify)
			{
				options_.on_notify(dispatch.notify.notify_message.text);
			}
		}
		else if (options_.custom_dispatch && options_.custom_dispatch(request, response))
		{
			// handled by custom_dispatch, response already filled in.
		}
		else
		{
			response.status = net::HttpStatus::NotFound;
			response.body = "{\"error\":\"not_found\"}";
		}
		write_response(static_cast<int>(client_socket), response);
	}

	close_socket(client_socket);
	return true;
}

} // namespace droidcli::tools
