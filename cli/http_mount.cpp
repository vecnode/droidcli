#include "http_mount.hpp"

#include "host.hpp"

namespace droidcli::cli {
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
		if (is_get && path == "/api/ollama/setup-status")
		{
			set_json(response, host.ollama_setup_status_json());
			return true;
		}
		if (is_post && path == "/api/ollama/install")
		{
			set_json(response, host.install_ollama());
			return true;
		}
		if (is_post && path == "/api/ollama/start")
		{
			set_json(response, host.start_ollama());
			return true;
		}
		if (is_post && path == "/api/ollama/pull")
		{
			set_json(response, host.pull_ollama_model(request.body));
			return true;
		}
		if (is_get && path == "/api/process/status")
		{
			set_json(response, host.build_process_status_json());
			return true;
		}
		if (is_post && path == "/api/run")
		{
			set_json(response, host.run_command(request.body));
			return true;
		}
		if (is_post && path == "/api/open")
		{
			set_json(response, host.open_application(request.body));
			return true;
		}
		if (is_post && path == "/api/apps/find")
		{
			set_json(response, host.find_applications_json(request.body));
			return true;
		}
		if (is_get && path == "/api/apps/open")
		{
			set_json(response, host.list_open_windows_json());
			return true;
		}

		// Filesystem-aware agent tools - droidcli executes these itself, no
		// external process or MCP server involved.
		if (is_post && path == "/api/fs/read")
		{
			set_json(response, host.read_file(request.body));
			return true;
		}
		if (is_post && path == "/api/fs/write")
		{
			set_json(response, host.write_file(request.body));
			return true;
		}
		if (is_post && path == "/api/fs/list")
		{
			set_json(response, host.list_dir(request.body));
			return true;
		}
		if (is_post && path == "/api/fs/stat")
		{
			set_json(response, host.stat_path(request.body));
			return true;
		}
		if (is_get && path == "/api/fs/cwd")
		{
			set_json(response, host.get_cwd_json());
			return true;
		}
		if (is_post && path == "/api/fs/which")
		{
			set_json(response, host.which_executable_json(request.body));
			return true;
		}

		if (is_post && path == "/api/agent/turn")
		{
			set_json(response, host.agent_turn(request.body));
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

} // namespace droidcli::cli
