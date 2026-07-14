#include "http_mount.hpp"

#include "host.hpp"

namespace metaagent::cli {
namespace {

void set_json(net::HttpResponse& response, const core::String& body, net::HttpStatus status = net::HttpStatus::Ok)
{
	response.status = status;
	response.content_type = "application/json";
	response.body = body;
}

// Splits "/api/connectors/<id>/<rest>" into id + rest ("" if there's no rest
// segment). prefix must include the trailing slash, e.g. "/api/connectors/".
bool split_id_and_rest(
	const core::String& path,
	const core::String& prefix,
	core::String& out_id,
	core::String& out_rest)
{
	if (path.rfind(prefix, 0) != 0)
	{
		return false;
	}
	const core::String remainder = path.substr(prefix.size());
	const size_t slash = remainder.find('/');
	if (slash == core::String::npos)
	{
		out_id = remainder;
		out_rest.clear();
	}
	else
	{
		out_id = remainder.substr(0, slash);
		out_rest = remainder.substr(slash);
	}
	return !out_id.empty();
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

tools::CustomRouteFn make_droidcli_route_dispatch(DroidHost& host)
{
	return [&host](const net::HttpRequest& request, net::HttpResponse& response) -> bool
	{
		const core::String& path = request.path;
		const bool is_get = request.method == net::HttpMethod::Get;
		const bool is_post = request.method == net::HttpMethod::Post;

		if (is_get && path == "/api/status")
		{
			set_json(response, host.build_status_json());
			return true;
		}
		if (is_get && path == "/api/network/status")
		{
			set_json(response, host.build_network_status_json());
			return true;
		}
		if (is_get && path == "/api/runtimes")
		{
			set_json(response, host.build_runtime_catalog_json());
			return true;
		}
		if (is_get && path == "/api/config")
		{
			set_json(response, host.build_config_json());
			return true;
		}
		if (is_post && path == "/api/config")
		{
			set_json(response, host.update_config(request.body));
			return true;
		}
		if (is_get && path == "/api/notify/log")
		{
			set_json(response, host.build_notify_log_json());
			return true;
		}
		if (is_get && path == "/api/app/log")
		{
			set_json(response, host.build_app_log_json());
			return true;
		}
		if (is_get && path == "/api/ollama/status")
		{
			set_json(response, host.build_ollama_status_json());
			return true;
		}
		if (is_post && path == "/api/ollama/config")
		{
			set_json(response, host.update_ollama_config(request.body));
			return true;
		}
		if (is_get && path == "/api/process/status")
		{
			set_json(response, host.build_process_status_json());
			return true;
		}
		if (is_post && path == "/api/command")
		{
			const core::String command_name = extract_command_name(request.body);
			if (command_name.empty())
			{
				set_json(response, "{\"error\":\"missing command\"}", net::HttpStatus::BadRequest);
				return true;
			}
			set_json(response, host.dispatch_command(command_name));
			return true;
		}

		// Connectors.
		if (is_get && path == "/api/connectors")
		{
			set_json(response, host.list_connectors_json());
			return true;
		}
		if (is_post && path == "/api/connectors")
		{
			set_json(response, host.register_connector(request.body));
			return true;
		}
		core::String connector_id;
		core::String rest;
		if (split_id_and_rest(path, "/api/connectors/", connector_id, rest))
		{
			if (is_get && rest == "/status")
			{
				set_json(response, host.connector_status_json(connector_id));
				return true;
			}
			if (is_post && rest == "/launch")
			{
				set_json(response, host.launch_connector(connector_id));
				return true;
			}
			if (is_post && rest == "/stop")
			{
				set_json(response, host.stop_connector(connector_id));
				return true;
			}
			if (is_post && rest == "/call")
			{
				const core::String call_path = net::extract_json_string_field(request.body, "path");
				const core::String method = net::extract_json_string_field(request.body, "method");
				const core::String payload = net::extract_json_string_field(request.body, "payload_json");
				set_json(response, host.call_connector(
					connector_id, call_path, method.empty() ? "POST" : method, payload));
				return true;
			}
		}

		// Tasks.
		if (is_get && path == "/api/tasks")
		{
			set_json(response, host.list_tasks_json());
			return true;
		}
		if (is_post && path == "/api/tasks")
		{
			set_json(response, host.enqueue_task(request.body));
			return true;
		}
		core::String task_id;
		core::String task_rest;
		if (is_get && split_id_and_rest(path, "/api/tasks/", task_id, task_rest) && task_rest.empty())
		{
			set_json(response, host.task_status_json(task_id));
			return true;
		}

		return false;
	};
}

} // namespace metaagent::cli
