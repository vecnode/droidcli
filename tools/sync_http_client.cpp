#include "tools/sync_http_client.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidNativeSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using NativeSocket = int;
constexpr NativeSocket kInvalidNativeSocket = -1;
#endif

namespace metaagent::tools {
namespace {

struct ParsedHttpUrl {
	core::String host;
	core::String path = "/";
	int32_t port = 80;
	bool valid = false;
};

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

void close_socket(const NativeSocket socket_handle)
{
#if defined(_WIN32)
	closesocket(socket_handle);
#else
	close(socket_handle);
#endif
}

ParsedHttpUrl parse_http_url(const core::String& url)
{
	ParsedHttpUrl parsed;
	const core::String scheme_prefix = "http://";
	if (url.rfind(scheme_prefix, 0) != 0)
	{
		return parsed;
	}

	core::String remainder = url.substr(scheme_prefix.size());
	const size_t path_index = remainder.find('/');
	if (path_index != core::String::npos)
	{
		parsed.path = remainder.substr(path_index);
		remainder = remainder.substr(0, path_index);
	}

	const size_t port_index = remainder.find(':');
	if (port_index != core::String::npos)
	{
		parsed.host = remainder.substr(0, port_index);
		parsed.port = std::max(1, std::atoi(remainder.substr(port_index + 1).c_str()));
	}
	else
	{
		parsed.host = remainder;
		parsed.port = 80;
	}

	parsed.valid = !parsed.host.empty();
	return parsed;
}

bool read_http_response(const NativeSocket socket_handle, int32_t& status_code_out, core::String& body_out)
{
	core::String buffer;
	char chunk[2048];
	while (buffer.find("\r\n\r\n") == core::String::npos)
	{
		const int received = recv(socket_handle, chunk, sizeof(chunk), 0);
		if (received <= 0)
		{
			return false;
		}
		buffer.append(chunk, static_cast<size_t>(received));
		if (buffer.size() > 1024 * 1024)
		{
			return false;
		}
	}

	const size_t header_end = buffer.find("\r\n\r\n");
	const core::String header_block = buffer.substr(0, header_end);
	body_out = buffer.substr(header_end + 4);

	const size_t status_index = header_block.find(' ');
	if (status_index != core::String::npos)
	{
		status_code_out = std::atoi(header_block.c_str() + static_cast<int>(status_index + 1));
	}

	int content_length = -1;
	std::istringstream header_stream(header_block);
	core::String line;
	while (std::getline(header_stream, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		core::String lower = line;
		for (char& character : lower)
		{
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
		if (lower.rfind("content-length:", 0) == 0)
		{
			content_length = std::atoi(line.substr(15).c_str());
		}
	}

	while (content_length >= 0 && static_cast<int>(body_out.size()) < content_length)
	{
		const int received = recv(socket_handle, chunk, sizeof(chunk), 0);
		if (received <= 0)
		{
			break;
		}
		body_out.append(chunk, static_cast<size_t>(received));
	}

	if (content_length >= 0)
	{
		body_out = body_out.substr(0, static_cast<size_t>(content_length));
	}

	return true;
}

} // namespace

bool sync_http_request(
	const core::String& method,
	const core::String& url,
	const core::String& body,
	int32_t& status_code_out,
	core::String& response_body_out)
{
	status_code_out = 0;
	response_body_out.clear();

	if (!ensure_socket_library())
	{
		return false;
	}

	const ParsedHttpUrl parsed = parse_http_url(url);
	if (!parsed.valid)
	{
		return false;
	}

	const NativeSocket client_socket = ::socket(AF_INET, SOCK_STREAM, 0);
	if (client_socket == kInvalidNativeSocket)
	{
		return false;
	}

	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<uint16_t>(parsed.port));
	if (parsed.host == "localhost" || parsed.host == "127.0.0.1")
	{
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	}
	else
	{
		hostent* host_entry = gethostbyname(parsed.host.c_str());
		if (!host_entry)
		{
			close_socket(client_socket);
			return false;
		}
		std::memcpy(&address.sin_addr, host_entry->h_addr_list[0], static_cast<size_t>(host_entry->h_length));
	}

	if (connect(client_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
	{
		close_socket(client_socket);
		return false;
	}

	core::String request = method + " " + parsed.path + " HTTP/1.1\r\n";
	request += "Host: " + parsed.host + "\r\n";
	request += "Connection: close\r\n";
	if (method == "POST")
	{
		request += "Content-Type: application/json\r\n";
		request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	}
	request += "\r\n";
	if (method == "POST")
	{
		request += body;
	}

	const char* data = request.c_str();
	size_t remaining = request.size();
	while (remaining > 0)
	{
		const int sent = send(client_socket, data, static_cast<int>(remaining), 0);
		if (sent <= 0)
		{
			close_socket(client_socket);
			return false;
		}
		data += sent;
		remaining -= static_cast<size_t>(sent);
	}

	const bool ok = read_http_response(client_socket, status_code_out, response_body_out);
	close_socket(client_socket);
	return ok;
}

bool sync_http_post_json(
	const core::String& url,
	const core::String& body,
	int32_t& status_code_out,
	core::String& response_body_out)
{
	return sync_http_request("POST", url, body, status_code_out, response_body_out);
}

bool sync_http_get(
	const core::String& url,
	int32_t& status_code_out,
	core::String& response_body_out)
{
	return sync_http_request("GET", url, {}, status_code_out, response_body_out);
}

} // namespace metaagent::tools
