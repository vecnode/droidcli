#include "metaagent_http_mount.hpp"

#include "metaagent_host.hpp"

#include <httplib.h>

namespace metaagent::app_host {
namespace {

net::HttpMethod to_http_method(const std::string& method)
{
	if (method == "GET")
	{
		return net::HttpMethod::Get;
	}
	if (method == "POST")
	{
		return net::HttpMethod::Post;
	}
	return net::HttpMethod::Unknown;
}

net::HttpRequest to_metaagent_request(const httplib::Request& request)
{
	net::HttpRequest converted;
	converted.method = to_http_method(request.method);
	converted.path = request.path;
	converted.query_string = request.target;
	const size_t query_index = converted.query_string.find('?');
	if (query_index != core::String::npos)
	{
		converted.query_string = converted.query_string.substr(query_index + 1);
	}
	else
	{
		converted.query_string.clear();
	}
	converted.body = request.body;
	return converted;
}

void apply_metaagent_response(const net::HttpResponse& response, httplib::Response& out_response)
{
	out_response.status = static_cast<int>(response.status);
	out_response.set_content(response.body, response.content_type.c_str());
	out_response.set_header("Cache-Control", "no-store");
	out_response.set_header("X-Content-Type-Options", "nosniff");
}

void apply_json_body(const core::String& body, httplib::Response& response)
{
	response.status = 200;
	response.set_content(body, "application/json");
	response.set_header("Cache-Control", "no-store");
	response.set_header("X-Content-Type-Options", "nosniff");
}

void handle_metaagent_route(MetaAgentHost& host, const httplib::Request& request, httplib::Response& response)
{
	const net::RouteDispatchResult dispatch = host.dispatch_route(to_metaagent_request(request));
	if (!dispatch.handled)
	{
		response.status = 404;
		response.set_content("{\"error\":\"not_found\"}", "application/json");
		return;
	}
	apply_metaagent_response(dispatch.response, response);
}

core::String extract_command_name(const core::String& body)
{
	core::String command = net::extract_json_string_field(body, "command");
	if (command.empty())
	{
		command = net::extract_json_string_field(body, "name");
	}
	return command;
}

} // namespace

void mount_metaagent_routes(httplib::Server& server, MetaAgentHost& host)
{
	server.Get("/health", [&host](const httplib::Request& request, httplib::Response& response)
	{
		handle_metaagent_route(host, request, response);
	});
	server.Get("/echo", [&host](const httplib::Request& request, httplib::Response& response)
	{
		handle_metaagent_route(host, request, response);
	});
	server.Post("/echo", [&host](const httplib::Request& request, httplib::Response& response)
	{
		handle_metaagent_route(host, request, response);
	});
	server.Post("/notify", [&host](const httplib::Request& request, httplib::Response& response)
	{
		handle_metaagent_route(host, request, response);
	});
	server.Post("/ai/chat", [&host](const httplib::Request& request, httplib::Response& response)
	{
		handle_metaagent_route(host, request, response);
	});

	server.Get("/api/status", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_status_json()},
			response);
	});

	server.Get("/api/network/status", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_network_status_json()},
			response);
	});

	server.Get("/api/runtimes", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_runtime_catalog_json()},
			response);
	});

	server.Post("/api/runtimes/ue5", [&host](const httplib::Request& request, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.set_ue5_runtimes_enabled(request.body)},
			response);
	});

	server.Get("/api/config", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_config_json()},
			response);
	});

	server.Post("/api/config", [&host](const httplib::Request& request, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.update_config(request.body)},
			response);
	});

	server.Get("/api/gui/catalog", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_gui_catalog_json()},
			response);
	});

	server.Get("/api/notify/log", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_notify_log_json()},
			response);
	});

	server.Get("/api/media/status", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_json_body(host.proxy_media_player_get("/api/status"), response);
	});

	server.Get("/api/media/clips", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_json_body(host.proxy_media_player_get("/api/clips"), response);
	});

	server.Get("/api/media/log", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_media_control_log_json()},
			response);
	});

	server.Get("/api/app/log", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_app_log_json()},
			response);
	});

	server.Post("/api/media/play", [&host](const httplib::Request& request, httplib::Response& response)
	{
		apply_json_body(host.proxy_media_player_post("/api/play", request.body), response);
	});

	server.Post("/api/media/stop", [&host](const httplib::Request& request, httplib::Response& response)
	{
		apply_json_body(host.proxy_media_player_post("/api/stop", request.body), response);
	});

	server.Post("/api/media/next", [&host](const httplib::Request& request, httplib::Response& response)
	{
		apply_json_body(host.proxy_media_player_post("/api/next", request.body), response);
	});

	server.Post("/api/media/previous", [&host](const httplib::Request& request, httplib::Response& response)
	{
		apply_json_body(host.proxy_media_player_post("/api/previous", request.body), response);
	});

	server.Post("/api/media/subtitles", [&host](const httplib::Request& request, httplib::Response& response)
	{
		apply_json_body(host.proxy_media_player_post("/api/subtitles", request.body), response);
	});

	server.Post(R"(/api/media/clips/(\d+))", [&host](const httplib::Request& request, httplib::Response& response)
	{
		const core::String path = "/api/clips/" + request.matches[1].str();
		apply_json_body(host.proxy_media_player_post(path, request.body), response);
	});

	server.Get("/api/ollama/status", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_ollama_status_json()},
			response);
	});

	server.Post("/api/ollama/config", [&host](const httplib::Request& request, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.update_ollama_config(request.body)},
			response);
	});

	server.Get("/api/adapter/status", [&host](const httplib::Request&, httplib::Response& response)
	{
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.build_adapter_status_json()},
			response);
	});

	server.Post("/api/adapter/summarize", [&host](const httplib::Request& request, httplib::Response& response)
	{
		apply_json_body(host.proxy_adapter_summarize(request.body), response);
	});

	server.Post("/api/command", [&host](const httplib::Request& request, httplib::Response& response)
	{
		const core::String command_name = extract_command_name(request.body);
		if (command_name.empty())
		{
			response.status = 400;
			response.set_content("{\"error\":\"missing command\"}", "application/json");
			return;
		}
		apply_metaagent_response(
			net::HttpResponse {net::HttpStatus::Ok, "application/json", host.dispatch_command(command_name)},
			response);
	});
}

} // namespace metaagent::app_host
