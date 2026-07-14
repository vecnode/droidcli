#include "host.hpp"

#include "tools/sync_http_client.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <thread>

namespace droidcli::cli {
namespace {

core::Array<core::String> parse_ollama_model_names(const core::String& tags_json)
{
	core::Array<core::String> names;
	const size_t models_index = tags_json.find("\"models\":");
	if (models_index == core::String::npos)
	{
		return names;
	}

	size_t cursor = models_index;
	while (cursor < tags_json.size())
	{
		const size_t name_key = tags_json.find("\"name\":", cursor);
		if (name_key == core::String::npos)
		{
			break;
		}

		const core::String name = net::extract_json_string_field(tags_json, "name", name_key);
		if (name.empty())
		{
			break;
		}

		bool duplicate = false;
		for (const core::String& existing : names)
		{
			if (existing == name)
			{
				duplicate = true;
				break;
			}
		}
		if (!duplicate)
		{
			names.push_back(name);
		}

		cursor = name_key + 6;
	}

	return names;
}

core::String strip_trailing_slashes(core::String value)
{
	while (!value.empty() && value.back() == '/')
	{
		value.pop_back();
	}
	return value;
}

core::String join_url(const core::String& base, const core::String& path)
{
	const core::String trimmed_base = strip_trailing_slashes(base);
	if (path.empty())
	{
		return trimmed_base;
	}
	return path.front() == '/' ? trimmed_base + path : trimmed_base + "/" + path;
}

} // namespace

void DroidHost::configure(const HostConfig& config)
{
	std::lock_guard<std::mutex> lock(mutex_);
	config_ = config;
}

void DroidHost::initialize()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);

		droidcli::initialize_defaults();

		session_.active = true;
		session_.map_name = "droidcli";
		session_.build_label = "daemon";
		session_.http_enabled = true;
		session_.http_router_bound = true;
		session_.features.networking = true;
		session_.features.ui = false;
		session_.features.ai = config_.enable_ai;
		session_.features.recording = true;

		wire_callbacks();

		language_ai_transport_.post_json = [](
			const core::String& url,
			const core::String& body,
			int32_t& status_code_out,
			core::String& response_body_out)
		{
			return tools::sync_http_post_json(url, body, status_code_out, response_body_out);
		};

		if (config_.enable_ai)
		{
			language_ai_.set_runtime_enabled(true);
			ai::OllamaConfig ollama_config;
			ollama_config.base_url = config_.ollama_url;
			ollama_config.model = config_.ollama_model;
			ollama_config.enabled = true;
			language_ai_.set_ollama_config(ollama_config);
			if (!config_.system_prompt.empty())
			{
				language_ai_.set_system_prompt(config_.system_prompt);
			}
		}
		else
		{
			language_ai_.set_runtime_enabled(false);
		}
	}

	append_app_log("host", "event", "droidcli host initialized", true);
}

void DroidHost::wire_callbacks()
{
	host_services_.toggle_recording = [this]()
	{
		recording_active_ = !recording_active_;
		return true;
	};
	host_services_.toggle_autopilot = [this]()
	{
		autopilot_enabled_ = !autopilot_enabled_;
		return true;
	};
	host_services_.query_recording = [this]()
	{
		runtime::RecordingSnapshot snapshot;
		snapshot.runtime_enabled = session_.features.recording;
		snapshot.capture_active = recording_active_;
		snapshot.status_text = recording_active_ ? "Recording: ON" : "Recording: OFF";
		return snapshot;
	};
	host_services_.query_ai = [this]()
	{
		runtime::AiSnapshot snapshot;
		snapshot.runtime_enabled = session_.features.ai;
		snapshot.autopilot_enabled = autopilot_enabled_;
		snapshot.status_text = autopilot_enabled_ ? "Autopilot: ON" : "Autopilot: OFF";
		return snapshot;
	};
}

void DroidHost::tick(const float delta_seconds)
{
	(void)delta_seconds;
	tick_tasks();
}

session::RuntimeSession& DroidHost::session()
{
	return session_;
}

const session::RuntimeSession& DroidHost::session() const
{
	return session_;
}

net::HandlerContext DroidHost::make_handler_context()
{
	net::HandlerContext context;
	context.session = session_;
	if (config_.enable_ai)
	{
		context.language_ai = &language_ai_;
		context.language_ai_transport = &language_ai_transport_;
	}
	return context;
}

net::RouteDispatchResult DroidHost::dispatch_route(const net::HttpRequest& request)
{
	std::lock_guard<std::mutex> lock(mutex_);
	net::RouteDispatchResult result = routes_.dispatch(request, make_handler_context());
	if (result.notify.has_notify_message)
	{
		on_notify(result.notify.notify_message.text);
	}
	return result;
}

void DroidHost::on_notify(const core::String& message)
{
	notify_log_.push_back(message);
	if (notify_log_.size() > 64)
	{
		notify_log_.erase(notify_log_.begin());
	}
	std::cout << "[notify] " << message << std::endl;
}

core::String DroidHost::build_notify_log_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream stream;
	stream << "{\"entries\":[";
	for (size_t index = 0; index < notify_log_.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		stream << '{';
		stream << net::json_string_field("message", notify_log_[index]);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

core::String DroidHost::build_status_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	const runtime::RecordingSnapshot recording = runtime::invoke_query_recording(host_services_);
	const runtime::AiSnapshot ai = runtime::invoke_query_ai(host_services_);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_string_field("host", "droidcli") << ',';
	stream << net::json_string_field("map", session_.map_name) << ',';
	stream << net::json_string_field("build", session_.build_label) << ',';
	stream << net::json_bool_field("active", session_.active) << ',';
	stream << net::json_bool_field("recording", recording.capture_active) << ',';
	stream << net::json_bool_field("autopilot", ai.autopilot_enabled);
	stream << '}';
	return stream.str();
}

core::String DroidHost::build_config_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ai_enabled", config_.enable_ai) << ',';
	stream << net::json_string_field("ollama_url", config_.ollama_url) << ',';
	stream << net::json_string_field("ollama_model", config_.ollama_model);
	stream << '}';
	return stream.str();
}

core::String DroidHost::update_config(const core::String& body)
{
	std::lock_guard<std::mutex> lock(mutex_);

	const core::String ollama_url = net::extract_json_string_field(body, "ollama_url");
	if (!ollama_url.empty())
	{
		config_.ollama_url = ollama_url;
	}

	const core::String ollama_model = net::extract_json_string_field(body, "ollama_model");
	if (!ollama_model.empty())
	{
		config_.ollama_model = ollama_model;
	}

	if (config_.enable_ai)
	{
		ai::OllamaConfig ollama_config;
		ollama_config.base_url = config_.ollama_url;
		ollama_config.model = config_.ollama_model;
		ollama_config.enabled = true;
		language_ai_.set_ollama_config(ollama_config);
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("success", true) << ',';
	stream << net::json_bool_field("ai_enabled", config_.enable_ai) << ',';
	stream << net::json_string_field("ollama_url", config_.ollama_url) << ',';
	stream << net::json_string_field("ollama_model", config_.ollama_model);
	stream << '}';
	return stream.str();
}

void DroidHost::apply_command_side_effects(const app::CommandId command)
{
	switch (command)
	{
	case app::CommandId::ToggleRecording:
		runtime::invoke_toggle_recording(host_services_);
		break;
	case app::CommandId::ToggleAutopilot:
		runtime::invoke_toggle_autopilot(host_services_);
		break;
	case app::CommandId::ToggleNetworkingRuntime:
		session_.features.networking = !session_.features.networking;
		session_.http_enabled = session_.features.networking;
		break;
	default:
		break;
	}
}

core::String DroidHost::dispatch_command(const core::String& command_name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const app::CommandId command = app::parse_command_name(command_name);
	const app::CommandResult validation = app::validate_command(command, session_);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_string_field("command", command_name) << ',';
	stream << net::json_bool_field("handled", validation.handled) << ',';
	stream << net::json_bool_field("success", validation.success) << ',';
	stream << net::json_string_field("message", validation.user_message);

	if (validation.success)
	{
		apply_command_side_effects(command);
	}

	stream << '}';
	return stream.str();
}

void DroidHost::append_app_log(
	const core::String& channel,
	const core::String& direction,
	const core::String& summary,
	const bool success)
{
	std::lock_guard<std::mutex> lock(mutex_);
	AppLogEntry entry;
	entry.timestamp = make_log_timestamp();
	entry.channel = channel;
	entry.direction = direction;
	entry.summary = summary;
	entry.success = success;
	app_log_.push_back(entry);
	if (app_log_.size() > 256)
	{
		app_log_.erase(app_log_.begin());
	}

	const core::String line = "[" + entry.timestamp + "] ["
		+ entry.channel + "] [" + entry.direction + "] " + entry.summary;
	if (success)
	{
		std::cout << line << std::endl;
	}
	else
	{
		std::cerr << line << std::endl;
	}
}

core::String DroidHost::make_log_timestamp()
{
	std::time_t raw_time = std::time(nullptr);
	std::tm local_time {};
#if defined(_WIN32)
	localtime_s(&local_time, &raw_time);
#else
	localtime_r(&raw_time, &local_time);
#endif

	char buffer[32] {};
	std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local_time);
	return buffer;
}

bool DroidHost::should_emit_periodic_log(
	const std::time_t now_utc,
	std::time_t& last_emit_utc,
	const int32_t min_interval_seconds)
{
	if (now_utc <= 0)
	{
		return true;
	}

	if (last_emit_utc == 0 || (now_utc - last_emit_utc) >= min_interval_seconds)
	{
		last_emit_utc = now_utc;
		return true;
	}

	return false;
}

core::String DroidHost::build_app_log_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream stream;
	stream << "{\"entries\":[";
	for (size_t index = 0; index < app_log_.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const AppLogEntry& entry = app_log_[index];
		stream << '{';
		stream << net::json_string_field("timestamp", entry.timestamp) << ',';
		stream << net::json_string_field("channel", entry.channel) << ',';
		stream << net::json_string_field("direction", entry.direction) << ',';
		stream << net::json_string_field("summary", entry.summary) << ',';
		stream << net::json_bool_field("success", entry.success);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

core::String DroidHost::build_network_status_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("networking_enabled", session_.features.networking) << ',';
	stream << "\"connector_count\":" << connectors_.list_connectors().size();
	stream << '}';
	return stream.str();
}

core::String DroidHost::build_runtime_catalog_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return app::build_runtime_catalog_json(session_);
}

core::String DroidHost::build_ollama_status_json()
{
	std::lock_guard<std::mutex> lock(mutex_);

	core::String tags_url = strip_trailing_slashes(config_.ollama_url) + "/api/tags";

	int32_t status_code = 0;
	core::String response_body;
	const bool transport_ok = tools::sync_http_get(tags_url, status_code, response_body);
	const bool online = config_.enable_ai && transport_ok && status_code >= 200 && status_code < 300;
	const core::Array<core::String> models =
		online ? parse_ollama_model_names(response_body) : core::Array<core::String> {};

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ai_enabled", config_.enable_ai) << ',';
	stream << net::json_string_field("ollama_url", config_.ollama_url) << ',';
	stream << net::json_string_field("model", config_.ollama_model) << ',';
	stream << net::json_bool_field("online", online) << ',';
	stream << "\"status_code\":" << status_code << ',';
	stream << "\"models\":[";
	for (size_t index = 0; index < models.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		stream << '"' << net::escape_json_string(models[index]) << '"';
	}
	stream << "]}";
	return stream.str();
}

core::String DroidHost::update_ollama_config(const core::String& body)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const core::String model = net::extract_json_string_field(body, "model");
	if (model.empty())
	{
		return "{"
			+ net::json_bool_field("success", false) + ","
			+ net::json_string_field("message", "Missing model field.")
			+ "}";
	}

	config_.ollama_model = model;
	if (config_.enable_ai)
	{
		ai::OllamaConfig ollama_config;
		ollama_config.base_url = config_.ollama_url;
		ollama_config.model = config_.ollama_model;
		ollama_config.enabled = true;
		language_ai_.set_ollama_config(ollama_config);
	}

	return "{"
		+ net::json_bool_field("success", true) + ","
		+ net::json_string_field("model", config_.ollama_model)
		+ "}";
}

core::String DroidHost::register_connector(const core::String& body)
{
	net::Connector connector;
	core::String error;
	if (!net::parse_connector_from_json(body, connector, error))
	{
		return "{" + net::json_bool_field("success", false) + ","
			+ net::json_string_field("error", error) + "}";
	}

	std::lock_guard<std::mutex> lock(mutex_);
	connectors_.register_connector(connector);
	return "{" + net::json_bool_field("success", true) + ","
		+ net::json_string_field("id", connector.id) + "}";
}

bool DroidHost::unregister_connector(const core::String& connector_id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	return connectors_.unregister_connector(connector_id);
}

core::String DroidHost::list_connectors_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return net::build_connectors_json(connectors_.list_connectors());
}

core::String DroidHost::connector_status_json(const core::String& connector_id)
{
	net::Connector connector;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const net::Connector* found = connectors_.find_connector(connector_id);
		if (found == nullptr)
		{
			return "{" + net::json_bool_field("ok", false) + ","
				+ net::json_string_field("error", "unknown connector") + "}";
		}
		connector = *found;
	}

	if (connector.kind == "launched_process")
	{
		const core::Array<ProcessInfo> processes = process_manager_.snapshot();
		for (const ProcessInfo& info : processes)
		{
			if (info.key == connector.id)
			{
				return "{" + net::json_bool_field("ok", true) + ","
					+ net::json_string_field("kind", connector.kind) + ","
					+ "\"pid\":" + std::to_string(info.pid) + ","
					+ net::json_bool_field("running", info.running) + ","
					+ net::json_string_field("status", info.status_text) + "}";
			}
		}
		return "{" + net::json_bool_field("ok", true) + ","
			+ net::json_string_field("kind", connector.kind) + ","
			+ net::json_bool_field("running", false) + ","
			+ net::json_string_field("status", "not launched") + "}";
	}

	// http_peer: probe /health on the connector's base URL.
	int32_t status_code = 0;
	core::String response_body;
	const bool transport_ok = tools::sync_http_get(join_url(connector.base_url, "/health"), status_code, response_body);
	const bool online = transport_ok && status_code >= 200 && status_code < 300;
	return "{" + net::json_bool_field("ok", true) + ","
		+ net::json_string_field("kind", connector.kind) + ","
		+ net::json_bool_field("online", online) + ","
		+ net::json_string_field("base_url", connector.base_url) + "}";
}

core::String DroidHost::launch_connector(const core::String& connector_id)
{
	net::Connector connector;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const net::Connector* found = connectors_.find_connector(connector_id);
		if (found == nullptr)
		{
			return "{" + net::json_bool_field("success", false) + ","
				+ net::json_string_field("error", "unknown connector") + "}";
		}
		connector = *found;
	}

	if (connector.kind != "launched_process")
	{
		return "{" + net::json_bool_field("success", false) + ","
			+ net::json_string_field("error", "connector is not a launched_process") + "}";
	}

	core::String error;
	const bool ok = process_manager_.launch(
		connector.id, connector.id, connector.work_dir, connector.launch_cmd, error);
	append_app_log("process", "out",
		ok ? ("launched connector: " + connector.id) : ("launch failed: " + error), ok);

	return "{" + net::json_bool_field("success", ok) + ","
		+ net::json_string_field("error", error) + "}";
}

core::String DroidHost::stop_connector(const core::String& connector_id)
{
	core::String error;
	const bool ok = process_manager_.stop(connector_id, error);
	append_app_log("process", "out",
		ok ? ("stopped connector: " + connector_id) : ("stop failed: " + error), ok);
	return "{" + net::json_bool_field("success", ok) + ","
		+ net::json_string_field("error", error) + "}";
}

core::String DroidHost::call_connector(
	const core::String& connector_id,
	const core::String& path,
	const core::String& method,
	const core::String& body)
{
	net::Connector connector;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const net::Connector* found = connectors_.find_connector(connector_id);
		if (found == nullptr)
		{
			return "{" + net::json_bool_field("ok", false) + ","
				+ net::json_string_field("error", "unknown connector") + "}";
		}
		connector = *found;
	}

	if (connector.kind != "http_peer")
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "connector is not an http_peer") + "}";
	}

	if (!connector.enabled)
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "connector is disabled") + "}";
	}

	const core::String url = join_url(connector.base_url, path);
	int32_t status_code = 0;
	core::String response_body;
	bool transport_ok = false;
	if (method == "GET")
	{
		transport_ok = tools::sync_http_get(url, status_code, response_body);
	}
	else
	{
		transport_ok = tools::sync_http_post_json(url, body, status_code, response_body);
	}

	append_app_log("connector", "out", method + " " + connector.id + path, transport_ok);

	if (!transport_ok)
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "connector unreachable") + "}";
	}

	if (response_body.empty())
	{
		response_body = "{}";
	}
	return response_body;
}

core::String DroidHost::enqueue_task(const core::String& body)
{
	app::Task task;
	core::String error;
	if (!app::parse_task_request_from_json(body, task, error))
	{
		return "{" + net::json_bool_field("success", false) + ","
			+ net::json_string_field("error", error) + "}";
	}

	std::lock_guard<std::mutex> lock(mutex_);
	const core::String id = tasks_.enqueue(task);
	return "{" + net::json_bool_field("success", true) + ","
		+ net::json_string_field("id", id) + "}";
}

core::String DroidHost::list_tasks_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return app::build_tasks_json(tasks_.list());
}

core::String DroidHost::task_status_json(const core::String& task_id) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	const auto found = tasks_.find(task_id);
	if (!found.has_value())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "unknown task") + "}";
	}
	return app::build_task_json(*found);
}

void DroidHost::tick_tasks()
{
	std::optional<app::Task> claimed;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		claimed = tasks_.claim_next();
	}
	if (!claimed.has_value())
	{
		return;
	}

	const app::Task task = *claimed;
	bool success = false;
	core::String error;

	if (task.command == "launch" && !task.connector_id.empty())
	{
		const core::String result = launch_connector(task.connector_id);
		success = result.find("\"success\":true") != core::String::npos;
		if (!success)
		{
			error = "launch_connector failed";
		}
	}
	else if (task.command == "stop" && !task.connector_id.empty())
	{
		const core::String result = stop_connector(task.connector_id);
		success = result.find("\"success\":true") != core::String::npos;
		if (!success)
		{
			error = "stop_connector failed";
		}
	}
	else if (!task.connector_id.empty())
	{
		// Treat any other command as an http_peer call: command is the request path.
		const core::String result = call_connector(task.connector_id, task.command, "POST", task.payload_json);
		success = result.find("\"ok\":false") == core::String::npos;
		if (!success)
		{
			error = result;
		}
	}
	else
	{
		error = "task has no connector_id and command is not launch/stop";
	}

	std::lock_guard<std::mutex> lock(mutex_);
	if (success)
	{
		tasks_.complete(task.id);
	}
	else
	{
		tasks_.fail(task.id, error);
	}
}

core::String DroidHost::build_process_status_json()
{
	const core::Array<ProcessInfo> processes = process_manager_.snapshot();

	std::ostringstream stream;
	stream << '{';
	stream << "\"processes\":[";
	for (size_t index = 0; index < processes.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const ProcessInfo& info = processes[index];
		stream << '{';
		stream << net::json_string_field("key", info.key) << ',';
		stream << net::json_string_field("label", info.label) << ',';
		stream << "\"pid\":" << info.pid << ',';
		stream << net::json_bool_field("running", info.running) << ',';
		stream << net::json_string_field("status", info.status_text);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

} // namespace droidcli::cli
