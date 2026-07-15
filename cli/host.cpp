#include "host.hpp"

#include "command_runner.hpp"
#include "tools/sync_http_client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
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

// Minimal integer field extractor, matching net/json.hpp's hand-rolled style
// (only string/bool extractors exist there). Returns false if the field is
// absent or not a plain integer literal.
bool extract_json_int_field(const core::String& json, const core::String& field_name, int32_t& out_value)
{
	const core::String needle = "\"" + field_name + "\":";
	const size_t field_index = json.find(needle);
	if (field_index == core::String::npos)
	{
		return false;
	}
	size_t cursor = field_index + needle.size();
	while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'))
	{
		++cursor;
	}
	const size_t start = cursor;
	if (cursor < json.size() && json[cursor] == '-')
	{
		++cursor;
	}
	while (cursor < json.size() && std::isdigit(static_cast<unsigned char>(json[cursor])))
	{
		++cursor;
	}
	if (cursor == start)
	{
		return false;
	}
	out_value = std::atoi(json.substr(start, cursor - start).c_str());
	return true;
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

	std::ostringstream stream;
	stream << '{';
	stream << net::json_string_field("host", "droidcli") << ',';
	stream << net::json_string_field("map", session_.map_name) << ',';
	stream << net::json_string_field("build", session_.build_label) << ',';
	stream << net::json_bool_field("active", session_.active) << ',';
	stream << net::json_bool_field("ai_enabled", config_.enable_ai) << ',';
	stream << "\"connector_count\":" << connectors_.list_connectors().size() << ',';
	stream << "\"task_count\":" << tasks_.list().size();
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

core::String DroidHost::ollama_setup_status_json()
{
	// "installed" - is an `ollama` binary on PATH. Windows-first (`where`);
	// POSIX gets a best-effort `which` fallback, matching the rest of this
	// codebase's Windows-first precedent (process_manager.cpp/command_runner.cpp).
#if defined(_WIN32)
	const CommandRunResult where_result = run_command_once("where ollama", "", 5000);
#else
	const CommandRunResult where_result = run_command_once("which ollama", "", 5000);
#endif
	const bool installed = where_result.launched && where_result.exit_code == 0
		&& !where_result.stdout_text.empty();

	core::String ollama_url_copy;
	core::String ollama_model_copy;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ollama_url_copy = config_.ollama_url;
		ollama_model_copy = config_.ollama_model;
	}

	const core::String tags_url = strip_trailing_slashes(ollama_url_copy) + "/api/tags";
	int32_t status_code = 0;
	core::String response_body;
	const bool transport_ok = tools::sync_http_get(tags_url, status_code, response_body);
	const bool online = transport_ok && status_code >= 200 && status_code < 300;
	const core::Array<core::String> models =
		online ? parse_ollama_model_names(response_body) : core::Array<core::String> {};

	bool configured_model_pulled = false;
	for (const core::String& name : models)
	{
		if (name == ollama_model_copy)
		{
			configured_model_pulled = true;
			break;
		}
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("installed", installed) << ',';
	stream << net::json_bool_field("online", online) << ',';
	stream << "\"models\":[";
	for (size_t index = 0; index < models.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		stream << '"' << net::escape_json_string(models[index]) << '"';
	}
	stream << "],";
	stream << net::json_string_field("configured_model", ollama_model_copy) << ',';
	stream << net::json_bool_field("configured_model_pulled", configured_model_pulled);
	stream << '}';
	return stream.str();
}

core::String DroidHost::install_ollama()
{
#if defined(_WIN32)
	const CommandRunResult where_winget = run_command_once("where winget", "", 5000);
	if (!where_winget.launched || where_winget.exit_code != 0)
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ "\"exit_code\":0,"
			+ net::json_string_field("stdout", "") + ","
			+ net::json_string_field("stderr", "") + ","
			+ net::json_string_field("error",
				"winget is not available on this system. Install Ollama manually from https://ollama.com/download.")
			+ "}";
	}

	const CommandRunResult result = run_command_once(
		"winget install --id Ollama.Ollama -e --source winget --accept-package-agreements --accept-source-agreements",
		"", 300000);
	const bool ok = result.launched && result.exit_code == 0;
	append_app_log("ollama", "out", ok ? "installed Ollama via winget" : "winget install failed", ok);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", ok) << ',';
	stream << "\"exit_code\":" << result.exit_code << ',';
	stream << net::json_string_field("stdout", result.stdout_text) << ',';
	stream << net::json_string_field("stderr", result.stderr_text) << ',';
	stream << net::json_string_field("error", result.error_message);
	stream << '}';
	return stream.str();
#else
	return "{" + net::json_bool_field("ok", false) + ","
		+ "\"exit_code\":0,"
		+ net::json_string_field("stdout", "") + ","
		+ net::json_string_field("stderr", "") + ","
		+ net::json_string_field("error",
			"automatic install is only implemented on Windows; install Ollama manually from https://ollama.com/download.")
		+ "}";
#endif
}

core::String DroidHost::start_ollama()
{
	core::String error;
	const bool launched = process_manager_.launch("__ollama__", "ollama serve", "", "ollama serve", error);
	if (!launched)
	{
		append_app_log("ollama", "out", "failed to launch ollama serve: " + error, false);
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_bool_field("online", false) + ","
			+ net::json_string_field("error", error) + "}";
	}

	int64_t pid = 0;
	for (const ProcessInfo& info : process_manager_.snapshot())
	{
		if (info.key == "__ollama__")
		{
			pid = info.pid;
			break;
		}
	}

	core::String ollama_url_copy;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ollama_url_copy = config_.ollama_url;
	}
	const core::String tags_url = strip_trailing_slashes(ollama_url_copy) + "/api/tags";

	// Ollama's own startup is usually near-instant once the process exists,
	// but give it a few beats before giving up on "online".
	bool online = false;
	for (int attempt = 0; attempt < 6 && !online; ++attempt)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		int32_t status_code = 0;
		core::String response_body;
		if (tools::sync_http_get(tags_url, status_code, response_body) && status_code >= 200 && status_code < 300)
		{
			online = true;
		}
	}

	append_app_log("ollama", "out",
		online ? "ollama serve came online" : "ollama serve launched but is not yet reachable", online);

	return "{" + net::json_bool_field("ok", true) + ","
		+ "\"pid\":" + std::to_string(pid) + ","
		+ net::json_bool_field("online", online) + "}";
}

core::String DroidHost::pull_ollama_model(const core::String& body)
{
	const core::String model = net::extract_json_string_field(body, "model");
	if (model.empty())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("model", "") + ","
			+ "\"exit_code\":0,"
			+ net::json_string_field("error", "missing model") + "}";
	}

	const CommandRunResult result = run_command_once("ollama pull " + model, "", 600000);
	const bool ok = result.launched && result.exit_code == 0;
	append_app_log("ollama", "out", ok ? ("pulled model: " + model) : ("pull failed: " + model), ok);

	if (ok)
	{
		update_ollama_config("{" + net::json_string_field("model", model) + "}");
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", ok) << ',';
	stream << net::json_string_field("model", model) << ',';
	stream << "\"exit_code\":" << result.exit_code << ',';
	stream << net::json_string_field("error",
		result.error_message.empty() && !ok ? result.stderr_text : result.error_message);
	stream << '}';
	return stream.str();
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
	core::String result_json;

	if (task.command == "run")
	{
		// No connector_id required: payload_json is {"command":"...","work_dir":"..."}.
		const core::String run_result = run_command(task.payload_json);
		result_json = run_result;
		success = run_result.find("\"launched\":true") != core::String::npos;
		if (!success)
		{
			error = run_result;
		}
	}
	else if (task.command == "launch" && !task.connector_id.empty())
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
		tasks_.complete(task.id, result_json);
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

core::String DroidHost::run_command(const core::String& body)
{
	const core::String command = net::extract_json_string_field(body, "command");
	const core::String work_dir = net::extract_json_string_field(body, "work_dir");
	int32_t timeout_ms = 30000;
	extract_json_int_field(body, "timeout_ms", timeout_ms);

	if (command.empty())
	{
		return "{" + net::json_bool_field("launched", false) + ","
			+ "\"exit_code\":0,"
			+ net::json_string_field("stdout", "") + ","
			+ net::json_string_field("stderr", "") + ","
			+ net::json_string_field("error", "missing command") + "}";
	}

	const CommandRunResult result = run_command_once(command, work_dir, timeout_ms);
	append_app_log("run", "out", command, result.error_message.empty());

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("launched", result.launched) << ',';
	stream << "\"exit_code\":" << result.exit_code << ',';
	stream << net::json_string_field("stdout", result.stdout_text) << ',';
	stream << net::json_string_field("stderr", result.stderr_text) << ',';
	stream << net::json_string_field("error", result.error_message);
	stream << '}';
	return stream.str();
}

core::String DroidHost::open_application(const core::String& body)
{
	const core::String path_or_name = net::extract_json_string_field(body, "path_or_name");
	const core::String args = net::extract_json_string_field(body, "args");
	const core::String work_dir = net::extract_json_string_field(body, "work_dir");

	if (path_or_name.empty())
	{
		return "{" + net::json_bool_field("launched", false) + ","
			+ "\"pid\":0,"
			+ net::json_string_field("error", "missing path_or_name") + "}";
	}

	const LaunchAppResult result = launch_application(path_or_name, args, work_dir);
	append_app_log("open", "out", path_or_name, result.launched);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("launched", result.launched) << ',';
	stream << "\"pid\":" << result.pid << ',';
	stream << net::json_string_field("error", result.error_message);
	stream << '}';
	return stream.str();
}

core::String DroidHost::read_file(const core::String& body)
{
	const core::String path = net::extract_json_string_field(body, "path");
	int32_t max_bytes = 65536;
	extract_json_int_field(body, "max_bytes", max_bytes);

	const FileReadResult result = cli::read_file(path, max_bytes);
	append_app_log("fs", "in", "read " + path, result.ok);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", result.ok) << ',';
	stream << net::json_string_field("content", result.content) << ',';
	stream << "\"size_bytes\":" << result.size_bytes << ',';
	stream << net::json_bool_field("truncated", result.truncated) << ',';
	stream << net::json_string_field("error", result.error_message);
	stream << '}';
	return stream.str();
}

core::String DroidHost::write_file(const core::String& body)
{
	const core::String path = net::extract_json_string_field(body, "path");
	const core::String content = net::extract_json_string_field(body, "content");
	bool append_mode = false;
	net::extract_json_bool_field(body, "append", append_mode);

	const FileWriteResult result = cli::write_file(path, content, append_mode);
	append_app_log("fs", "out", (append_mode ? "append " : "write ") + path, result.ok);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", result.ok) << ',';
	stream << "\"bytes_written\":" << result.bytes_written << ',';
	stream << net::json_string_field("error", result.error_message);
	stream << '}';
	return stream.str();
}

core::String DroidHost::list_dir(const core::String& body)
{
	const core::String path = net::extract_json_string_field(body, "path");
	const ListDirResult result = cli::list_dir(path);
	append_app_log("fs", "in", "list " + (path.empty() ? core::String(".") : path), result.ok);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", result.ok) << ',';
	stream << "\"entries\":[";
	for (size_t index = 0; index < result.entries.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const DirEntryInfo& entry = result.entries[index];
		stream << '{';
		stream << net::json_string_field("name", entry.name) << ',';
		stream << net::json_bool_field("is_dir", entry.is_dir) << ',';
		stream << "\"size_bytes\":" << entry.size_bytes;
		stream << '}';
	}
	stream << "],";
	stream << net::json_string_field("error", result.error_message);
	stream << '}';
	return stream.str();
}

core::String DroidHost::stat_path(const core::String& body)
{
	const core::String path = net::extract_json_string_field(body, "path");
	const StatResult result = cli::stat_path(path);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", result.ok) << ',';
	stream << net::json_bool_field("exists", result.exists) << ',';
	stream << net::json_bool_field("is_dir", result.is_dir) << ',';
	stream << net::json_bool_field("is_file", result.is_file) << ',';
	stream << "\"size_bytes\":" << result.size_bytes << ',';
	stream << net::json_string_field("error", result.error_message);
	stream << '}';
	return stream.str();
}

core::String DroidHost::get_cwd_json() const
{
	return "{" + net::json_string_field("cwd", get_current_working_directory()) + "}";
}

core::String DroidHost::which_executable_json(const core::String& body)
{
	const core::String name = net::extract_json_string_field(body, "name");
	const WhichResult result = which_executable(name);

	return "{"
		+ net::json_bool_field("ok", result.ok) + ","
		+ net::json_string_field("path", result.resolved_path) + ","
		+ net::json_string_field("error", result.error_message)
		+ "}";
}

core::Array<ai::ToolDefinition> DroidHost::agent_tool_definitions() const
{
	core::Array<ai::ToolDefinition> tools;

	tools.push_back(ai::ToolDefinition{
		"list_connectors",
		"List every registered connector (http_peer or launched_process), with kind, base_url/launch_cmd, and enabled state. Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"connector_status",
		"Get the live status of one connector by id: for a launched_process, its PID and running state; for an http_peer, whether its /health endpoint is reachable.",
		"{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\",\"description\":\"connector id\"}},\"required\":[\"id\"]}"});

	tools.push_back(ai::ToolDefinition{
		"launch_connector",
		"Launch a launched_process connector by id (starts the configured command in its work_dir). No effect on http_peer connectors.",
		"{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\",\"description\":\"connector id\"}},\"required\":[\"id\"]}"});

	tools.push_back(ai::ToolDefinition{
		"stop_connector",
		"Stop a running launched_process connector by id.",
		"{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\",\"description\":\"connector id\"}},\"required\":[\"id\"]}"});

	tools.push_back(ai::ToolDefinition{
		"call_connector",
		"Call an HTTP path on an http_peer connector by id, e.g. to invoke one of its endpoints. payload_json is a raw JSON string sent as the request body for POST.",
		"{\"type\":\"object\",\"properties\":{"
		"\"id\":{\"type\":\"string\",\"description\":\"connector id\"},"
		"\"path\":{\"type\":\"string\",\"description\":\"request path, e.g. /status\"},"
		"\"method\":{\"type\":\"string\",\"description\":\"GET or POST, default POST\"},"
		"\"payload_json\":{\"type\":\"string\",\"description\":\"raw JSON body for POST, optional\"}"
		"},\"required\":[\"id\",\"path\"]}"});

	tools.push_back(ai::ToolDefinition{
		"enqueue_task",
		"Queue a task for droidcli's background task loop to process later: \"launch\"/\"stop\" a connector_id, \"run\" a shell command (payload_json {\"command\":..,\"work_dir\":..}), or any other command string treated as an HTTP path called on connector_id.",
		"{\"type\":\"object\",\"properties\":{"
		"\"connector_id\":{\"type\":\"string\",\"description\":\"optional, required for launch/stop/http-path commands\"},"
		"\"command\":{\"type\":\"string\",\"description\":\"launch | stop | run | <http path>\"},"
		"\"payload_json\":{\"type\":\"string\",\"description\":\"optional raw JSON payload\"}"
		"},\"required\":[\"command\"]}"});

	tools.push_back(ai::ToolDefinition{
		"list_tasks",
		"List all tasks in the task queue with their status (pending/running/done/failed). Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"run_command",
		"Run a one-shot shell command synchronously and capture its stdout/stderr/exit code. This executes arbitrary shell commands on the host machine - use only for tasks the user actually asked for.",
		"{\"type\":\"object\",\"properties\":{"
		"\"command\":{\"type\":\"string\",\"description\":\"the shell command to run\"},"
		"\"work_dir\":{\"type\":\"string\",\"description\":\"optional working directory\"}"
		"},\"required\":[\"command\"]}"});

	tools.push_back(ai::ToolDefinition{
		"open_application",
		"Open/launch a GUI application (e.g. Notepad, a browser, an image viewer) so the user can see and use it. Detached - does not wait for it to close and does not capture output. Use this instead of run_command for opening apps, since run_command waits for the process to exit and GUI apps don't exit on their own. path_or_name is resolved against the Windows App Paths registry (how most installed apps register themselves, e.g. 'chrome' even though it's not on PATH), then PATH, then as a direct path. If you're not confident which exact app/path the user means (ambiguous name, multiple plausible matches, or a prior call failed to resolve it), ask the user to confirm the name or full path rather than guessing at one.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path_or_name\":{\"type\":\"string\",\"description\":\"executable name (e.g. 'chrome', 'notepad.exe') or a full path\"},"
		"\"args\":{\"type\":\"string\",\"description\":\"optional command-line arguments\"},"
		"\"work_dir\":{\"type\":\"string\",\"description\":\"optional working directory\"}"
		"},\"required\":[\"path_or_name\"]}"});

	tools.push_back(ai::ToolDefinition{
		"read_file",
		"Read a file's contents from the local filesystem. Caps the read at max_bytes (default 65536) to avoid overloading context - the result reports whether the file was truncated.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\",\"description\":\"file path, absolute or relative to droidcli's working directory\"},"
		"\"max_bytes\":{\"type\":\"integer\",\"description\":\"optional read cap, default 65536\"}"
		"},\"required\":[\"path\"]}"});

	tools.push_back(ai::ToolDefinition{
		"write_file",
		"Create a new file, or overwrite/append to an existing one, on the local filesystem. Creates the file if it doesn't exist yet (this is also the tool to use for 'create a file'), and creates any missing parent directories. Overwrites by default - use only for tasks the user actually asked for.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\",\"description\":\"file path to write\"},"
		"\"content\":{\"type\":\"string\",\"description\":\"text to write\"},"
		"\"append\":{\"type\":\"boolean\",\"description\":\"append instead of overwrite, default false\"}"
		"},\"required\":[\"path\",\"content\"]}"});

	tools.push_back(ai::ToolDefinition{
		"list_dir",
		"List the entries (name, is_dir, size_bytes) of a directory, one level deep. Omit path to list droidcli's current working directory.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\",\"description\":\"directory path, optional (defaults to the current working directory)\"}"
		"}}"});

	tools.push_back(ai::ToolDefinition{
		"stat_path",
		"Check whether a path exists and whether it's a file or directory, without reading its content.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\",\"description\":\"path to check\"}"
		"},\"required\":[\"path\"]}"});

	tools.push_back(ai::ToolDefinition{
		"get_cwd",
		"Get droidcli's current working directory as an absolute path. Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"which",
		"Resolve an executable name to its full path by searching PATH, the same lookup a shell does before running a bare command name. Use this to check whether a tool is installed before trying to run it.",
		"{\"type\":\"object\",\"properties\":{"
		"\"name\":{\"type\":\"string\",\"description\":\"executable name, e.g. 'git' or 'python'\"}"
		"},\"required\":[\"name\"]}"});

	return tools;
}

core::String DroidHost::execute_agent_tool(const core::String& tool_name, const core::String& arguments_json)
{
	if (tool_name == "list_connectors")
	{
		return list_connectors_json();
	}
	if (tool_name == "connector_status")
	{
		return connector_status_json(net::extract_json_string_field(arguments_json, "id"));
	}
	if (tool_name == "launch_connector")
	{
		return launch_connector(net::extract_json_string_field(arguments_json, "id"));
	}
	if (tool_name == "stop_connector")
	{
		return stop_connector(net::extract_json_string_field(arguments_json, "id"));
	}
	if (tool_name == "call_connector")
	{
		const core::String id = net::extract_json_string_field(arguments_json, "id");
		const core::String path = net::extract_json_string_field(arguments_json, "path");
		core::String method = net::extract_json_string_field(arguments_json, "method");
		const core::String payload = net::extract_json_string_field(arguments_json, "payload_json");
		return call_connector(id, path, method.empty() ? "POST" : method, payload);
	}
	if (tool_name == "enqueue_task")
	{
		return enqueue_task(arguments_json);
	}
	if (tool_name == "list_tasks")
	{
		return list_tasks_json();
	}
	if (tool_name == "run_command")
	{
		return run_command(arguments_json);
	}
	if (tool_name == "open_application")
	{
		return open_application(arguments_json);
	}
	if (tool_name == "read_file")
	{
		return read_file(arguments_json);
	}
	if (tool_name == "write_file")
	{
		return write_file(arguments_json);
	}
	if (tool_name == "list_dir")
	{
		return list_dir(arguments_json);
	}
	if (tool_name == "stat_path")
	{
		return stat_path(arguments_json);
	}
	if (tool_name == "get_cwd")
	{
		return get_cwd_json();
	}
	if (tool_name == "which")
	{
		return which_executable_json(arguments_json);
	}

	return "{" + net::json_bool_field("ok", false) + ","
		+ net::json_string_field("error", "unknown tool: " + tool_name) + "}";
}

core::String DroidHost::agent_turn(const core::String& body)
{
	const core::String user_message = net::extract_json_string_field(body, "message");
	bool clear = false;
	net::extract_json_bool_field(body, "clear", clear);

	if (clear)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		agent_transcript_.clear();
	}

	if (user_message.empty())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "missing message") + "}";
	}

	if (!config_.enable_ai)
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "AI is disabled (--no-ai)") + "}";
	}

	ai::OllamaConfig ollama_config;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ollama_config.base_url = config_.ollama_url;
		ollama_config.model = config_.ollama_model;
		ollama_config.enabled = true;

		if (agent_transcript_.empty() && !config_.system_prompt.empty())
		{
			agent_transcript_.push_back(ai::ChatMessage{ai::ChatRole::System, config_.system_prompt});
		}
		agent_transcript_.push_back(ai::ChatMessage{ai::ChatRole::User, user_message});
	}

	const core::Array<ai::ToolDefinition> tools = agent_tool_definitions();

	std::ostringstream actions_stream;
	actions_stream << '[';
	bool first_action = true;

	core::String final_assistant_text;
	bool budget_exhausted = false;
	constexpr int kMaxHops = 5;

	for (int hop = 0; hop < kMaxHops; ++hop)
	{
		core::Array<ai::ChatMessage> transcript_copy;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			transcript_copy = agent_transcript_;
		}

		const ai::OllamaOutboundRequest request = ai::build_ollama_chat_request(ollama_config, transcript_copy, tools);
		if (!request.valid)
		{
			return "{" + net::json_bool_field("ok", false) + ","
				+ net::json_string_field("error", request.error_message) + "}";
		}

		int32_t status_code = 0;
		core::String response_body;
		const bool transport_ok = language_ai_transport_.post_json
			? language_ai_transport_.post_json(request.url, request.body, status_code, response_body)
			: false;

		const ai::OllamaChatResponse response = ai::parse_ollama_chat_response(status_code, response_body, transport_ok);
		if (!response.transport_ok || !response.http_success)
		{
			return "{" + net::json_bool_field("ok", false) + ","
				+ net::json_string_field("error", response.error_message.empty()
					? "Ollama request failed" : response.error_message) + "}";
		}

		{
			std::lock_guard<std::mutex> lock(mutex_);
			agent_transcript_.push_back(ai::ChatMessage{ai::ChatRole::Assistant, response.assistant_message});
		}

		if (response.tool_calls.empty())
		{
			final_assistant_text = response.assistant_message;
			break;
		}

		for (const ai::ToolCall& call : response.tool_calls)
		{
			const core::String tool_result = execute_agent_tool(call.name, call.arguments_json);

			{
				std::lock_guard<std::mutex> lock(mutex_);
				agent_transcript_.push_back(ai::ChatMessage{ai::ChatRole::Tool, tool_result});
			}

			if (!first_action)
			{
				actions_stream << ',';
			}
			first_action = false;
			actions_stream << '{';
			actions_stream << net::json_string_field("tool", call.name) << ',';
			actions_stream << net::json_string_field("arguments_json", call.arguments_json) << ',';
			actions_stream << net::json_string_field("result_json", tool_result);
			actions_stream << '}';
		}

		if (hop == kMaxHops - 1)
		{
			budget_exhausted = true;
			final_assistant_text = response.assistant_message;
		}
	}

	actions_stream << ']';

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", true) << ',';
	stream << net::json_string_field("assistant", final_assistant_text) << ',';
	if (budget_exhausted)
	{
		stream << net::json_bool_field("budget_exhausted", true) << ',';
	}
	stream << "\"actions\":" << actions_stream.str();
	stream << '}';
	return stream.str();
}

} // namespace droidcli::cli
