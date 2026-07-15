#include "host.hpp"

#include "app_index.hpp"
#include "command_runner.hpp"
#include "intent/open_intent.hpp"
#include "tools/sync_http_client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

namespace droidcli::cli {
namespace {

// Lowercases and strips everything but letters/digits, so "Note Pad",
// "note-pad", and "NOTEPAD" all normalize to the same "notepad" key - name
// matching should be resilient to case and to spacing/punctuation variants a
// user (or a model paraphrasing the user) might type, not just case.
core::String normalize_for_match(const core::String& value)
{
	core::String result;
	result.reserve(value.size());
	for (const unsigned char c : value)
	{
		if (std::isalnum(c))
		{
			result += static_cast<char>(std::tolower(c));
		}
	}
	return result;
}

// Best-effort match of `query` against the installed-apps index, insensitive
// to case and to spacing/punctuation: exact normalized match first, then a
// substring match either direction (so "chrome" matches "Google Chrome",
// "kicad" matches "KiCad 8.0", and "note pad" / "NotePad" both match
// "Notepad"). Returns an empty path if nothing plausible is found.
core::String find_installed_app_match(const core::Array<InstalledApp>& apps, const core::String& query)
{
	if (query.empty())
	{
		return {};
	}
	const core::String normalized_query = normalize_for_match(query);
	if (normalized_query.empty())
	{
		return {};
	}

	for (const InstalledApp& app : apps)
	{
		if (normalize_for_match(app.name) == normalized_query)
		{
			return app.path;
		}
	}
	for (const InstalledApp& app : apps)
	{
		const core::String normalized_name = normalize_for_match(app.name);
		if (normalized_name.find(normalized_query) != core::String::npos
			|| normalized_query.find(normalized_name) != core::String::npos)
		{
			return app.path;
		}
	}
	return {};
}

// Same matching rule as find_installed_app_match, but returns every
// plausible match (capped at max_results) instead of just the first - the
// deterministic quick-open flow needs to know whether a query is ambiguous
// (more than one installed app could be meant), which a single best-match
// path can't tell it.
core::Array<InstalledApp> collect_installed_app_matches(
	const core::Array<InstalledApp>& apps, const core::String& query, size_t max_results)
{
	core::Array<InstalledApp> matches;
	const core::String normalized_query = normalize_for_match(query);
	if (normalized_query.empty())
	{
		return matches;
	}

	for (const InstalledApp& app : apps)
	{
		if (normalize_for_match(app.name) == normalized_query)
		{
			// An exact normalized-name match is unambiguous regardless of
			// how many substring matches also exist - return it alone.
			matches.clear();
			matches.push_back(app);
			return matches;
		}
	}
	for (const InstalledApp& app : apps)
	{
		const core::String normalized_name = normalize_for_match(app.name);
		if (normalized_name.find(normalized_query) != core::String::npos
			|| normalized_query.find(normalized_name) != core::String::npos)
		{
			matches.push_back(app);
			if (matches.size() >= max_results)
			{
				break;
			}
		}
	}
	return matches;
}

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

// Whether the agent-turn loop must pause and get the user's explicit
// approval before executing this tool, rather than auto-running it. Only
// side-effecting tools are gated - anything read-only (list_dir, get_cwd,
// get_system_info, which, list_connectors, list_tasks, list_open_windows,
// find_application, connector_status, read_file, stat_path, call_connector)
// keeps auto-running, since gating those would only make the agent slower
// to answer plain questions for no safety benefit.
bool tool_call_requires_approval(const core::String& tool_name)
{
	return tool_name == "run_command"
		|| tool_name == "run_ffmpeg"
		|| tool_name == "write_file"
		|| tool_name == "open_application"
		|| tool_name == "launch_connector"
		|| tool_name == "stop_connector"
		|| tool_name == "enqueue_task";
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

		// Queried once, up front, so every route/tool/log line that reports
		// "where droidcli is running" (build_system_info_json, the
		// get_system_info agent tool, the system prompt below) agrees with
		// each other instead of re-querying the OS independently.
		system_info_ = get_system_info();

		// Durable session log: logs/log.jsonl accumulates across restarts so
		// a crash or a bug report can be diagnosed after the fact, not just
		// while the process happens to still be up. Structured JSONL (one
		// JSON object per line, see append_app_log()), not the bracketed
		// plain-text format the console gets. Created relative to the
		// working directory droidcli was launched from - if that directory
		// isn't writable, log_file_ just stays closed and append_app_log()
		// silently skips the file write (console/in-memory logging still
		// works either way).
		std::error_code log_dir_error;
		std::filesystem::create_directories("logs", log_dir_error);
		log_file_.open("logs/log.jsonl", std::ios::app);
		if (log_file_)
		{
			log_file_ << "{" << net::json_string_field("event", "session_started") << ","
				<< net::json_string_field("ts", make_full_log_timestamp()) << "}" << std::endl;
			log_file_ << "{" << net::json_string_field("event", "system_detected") << ","
				<< net::json_string_field("os_name", system_info_.os_name) << ","
				<< net::json_string_field("os_version", system_info_.os_version) << ","
				<< net::json_string_field("architecture", system_info_.architecture) << ","
				<< net::json_string_field("hostname", system_info_.hostname) << ","
				<< net::json_string_field("cwd", system_info_.cwd) << ","
				<< net::json_string_field("ts", make_full_log_timestamp()) << "}" << std::endl;
		}

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
				// Appended rather than baked into HostConfig::system_prompt's
				// static default text, since system_info_ is only known once
				// initialize() actually queries the OS.
				const core::String prompt_with_system_info = config_.system_prompt
					+ " You are currently running on " + system_info_.os_name
					+ " " + system_info_.os_version + " (" + system_info_.architecture
					+ "), hostname " + system_info_.hostname
					+ ", working directory " + system_info_.cwd
					+ " - call get_system_info if you need these details again mid-conversation.";
				language_ai_.set_system_prompt(prompt_with_system_info);
			}
		}
		else
		{
			language_ai_.set_runtime_enabled(false);
		}

		// Scan once at startup, not per-lookup: this walks potentially
		// hundreds of registry keys (Windows Uninstall/Add-Remove-Programs
		// entries) and touches disk to resolve some paths. Lets
		// open_application()/find_application resolve names like "Blender"
		// or "KiCad" that installers never add to PATH or the App Paths
		// registry key - most installers only ever register an Add/Remove
		// Programs entry.
		installed_apps_ = scan_installed_applications();

		// Persistent agent-turn memory (see "Persistent memory" in
		// ARCHITECTURE.md's extension plan) - a fresh session id every
		// process start (no auto-resume; a caller opts in by passing a
		// prior session_id explicitly, see agent_turn()). A failed open()
		// (bad working directory, disk full) leaves memory_store_ closed;
		// record_agent_message()/history routes degrade to in-memory-only
		// behavior rather than crashing the daemon over it.
		current_session_id_ = generate_session_id();
		std::error_code db_dir_error;
		std::filesystem::create_directories("db", db_dir_error);
		if (!memory_store_.open("db/droidcli_memory.sqlite3"))
		{
			append_app_log("host", "event", "warning: could not open db/droidcli_memory.sqlite3 - agent history will not persist across restarts", false);
		}
	}

	append_app_log("host", "event",
		"droidcli host initialized (" + std::to_string(installed_apps_.size()) + " installed apps indexed)", true);
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
	const bool success,
	const core::String& session_id)
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

	// Console/stderr output stays a human-readable line - this is for a
	// person watching the terminal, not for durable structured storage.
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

	// Durable file log (logs/log.jsonl) is structured JSONL - one JSON
	// object per line, no bracketed-text formatting - so any tool can parse
	// it without re-deriving the console format's escaping rules. See
	// "Structured JSONL logging" in ARCHITECTURE.md's extension plan.
	if (log_file_)
	{
		core::String json_line = "{"
			+ net::json_string_field("ts", make_full_log_timestamp()) + ","
			+ net::json_string_field("channel", channel) + ","
			+ net::json_string_field("direction", direction) + ","
			+ net::json_string_field("summary", summary) + ","
			+ net::json_bool_field("success", success);
		if (!session_id.empty())
		{
			json_line += "," + net::json_string_field("session_id", session_id);
		}
		json_line += "}";
		log_file_ << json_line << std::endl;
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

core::String DroidHost::make_full_log_timestamp()
{
	std::time_t raw_time = std::time(nullptr);
	std::tm local_time {};
#if defined(_WIN32)
	localtime_s(&local_time, &raw_time);
#else
	localtime_r(&raw_time, &local_time);
#endif

	char buffer[32] {};
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time);
	return buffer;
}

core::String DroidHost::generate_session_id()
{
	std::time_t raw_time = std::time(nullptr);
	std::tm local_time {};
#if defined(_WIN32)
	localtime_s(&local_time, &raw_time);
#else
	localtime_r(&raw_time, &local_time);
#endif

	char buffer[32] {};
	std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%S", &local_time);

	// A timestamp alone can collide if two sessions start within the same
	// second (e.g. --no-ai smoke tests launched back to back); a few bits
	// of the address of a stack-local disambiguate without pulling in a
	// UUID/random dependency for something that only needs to be
	// locally-unique, not globally-unique or unpredictable.
	int disambiguator = 0;
	const auto address_bits = reinterpret_cast<std::uintptr_t>(&disambiguator);

	std::ostringstream stream;
	stream << buffer << "-" << std::hex << (address_bits & 0xffff);
	return stream.str();
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

core::String DroidHost::run_ffmpeg_json(const core::String& body)
{
	const core::String args = net::extract_json_string_field(body, "args");
	const core::String work_dir = net::extract_json_string_field(body, "work_dir");
	int32_t timeout_ms = 120000;
	extract_json_int_field(body, "timeout_ms", timeout_ms);

	if (args.empty())
	{
		return "{" + net::json_bool_field("launched", false) + ","
			+ "\"exit_code\":0,"
			+ net::json_string_field("stdout", "") + ","
			+ net::json_string_field("stderr", "") + ","
			+ net::json_string_field("error", "missing args") + "}";
	}

	const CommandRunResult result = run_ffmpeg(args, work_dir, timeout_ms);
	append_app_log("ffmpeg", "out", args, result.error_message.empty());

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

	// App Paths registry / raw PATH search first (launch_application's own
	// resolution) - if that fails, fall back to the installed-apps index
	// (scanned once at startup from the Uninstall/Add-Remove-Programs
	// registry), which covers apps that never registered themselves on
	// PATH or in App Paths at all.
	LaunchAppResult result = launch_application(path_or_name, args, work_dir);
	if (!result.launched)
	{
		core::String indexed_path;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			indexed_path = find_installed_app_match(installed_apps_, path_or_name);
		}
		if (!indexed_path.empty())
		{
			result = launch_application(indexed_path, args, work_dir);
		}
	}

	append_app_log("open", "out", path_or_name, result.launched);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("launched", result.launched) << ',';
	stream << "\"pid\":" << result.pid << ',';
	stream << net::json_string_field("error", result.error_message);
	stream << '}';
	return stream.str();
}

core::String DroidHost::find_applications_json(const core::String& body) const
{
	const core::String query = net::extract_json_string_field(body, "query");

	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream stream;
	stream << "{\"matches\":[";
	bool first = true;
	const core::String normalized_query = normalize_for_match(query);
	for (const InstalledApp& app : installed_apps_)
	{
		if (!normalized_query.empty() && normalize_for_match(app.name).find(normalized_query) == core::String::npos)
		{
			continue;
		}
		if (!first)
		{
			stream << ',';
		}
		first = false;
		stream << '{';
		stream << net::json_string_field("name", app.name) << ',';
		stream << net::json_string_field("path", app.path);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

core::String DroidHost::try_quick_open_json(const core::String& body) const
{
	const core::String message = net::extract_json_string_field(body, "message");
	const intent::OpenIntent parsed = intent::parse_open_intent(message);

	std::ostringstream stream;
	stream << '{' << net::json_bool_field("matched", parsed.matched);
	if (!parsed.matched)
	{
		stream << '}';
		return stream.str();
	}
	stream << ',' << net::json_string_field("app_name", parsed.app_name);

	core::Array<InstalledApp> candidates;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		candidates = collect_installed_app_matches(installed_apps_, parsed.app_name, 5);
	}

	stream << ',' << net::json_bool_field("ambiguous", candidates.size() > 1);
	if (candidates.size() == 1)
	{
		stream << ',' << net::json_string_field("resolved_name", candidates[0].name);
		stream << ',' << net::json_string_field("resolved_path", candidates[0].path);
	}
	stream << ",\"candidates\":[";
	for (size_t index = 0; index < candidates.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		stream << '{' << net::json_string_field("name", candidates[index].name) << ','
			<< net::json_string_field("path", candidates[index].path) << '}';
	}
	stream << "]}";
	return stream.str();
}

core::String DroidHost::list_open_windows_json() const
{
	const core::Array<OpenWindowInfo> windows = cli::list_open_windows();

	std::ostringstream stream;
	stream << "{\"windows\":[";
	for (size_t index = 0; index < windows.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const OpenWindowInfo& window = windows[index];
		stream << '{';
		stream << net::json_string_field("title", window.title) << ',';
		stream << net::json_string_field("process_name", window.process_name) << ',';
		stream << "\"pid\":" << window.pid;
		stream << '}';
	}
	stream << "]}";
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

core::String DroidHost::build_system_info_json() const
{
	return "{"
		+ net::json_string_field("os_name", system_info_.os_name) + ","
		+ net::json_string_field("os_version", system_info_.os_version) + ","
		+ net::json_string_field("architecture", system_info_.architecture) + ","
		+ net::json_string_field("hostname", system_info_.hostname) + ","
		+ net::json_string_field("username", system_info_.username) + ","
		+ net::json_string_field("cwd", system_info_.cwd)
		+ "}";
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
		"find_application",
		"Search the index of applications on this machine (scanned once at startup from Windows' Add/Remove Programs registry, plus built-in accessories like Notepad/Calculator/Paint/Command Prompt/PowerShell that don't appear in that registry) for a name matching or containing the query - matching ignores case and spacing, so 'notepad', 'NotePad', and 'note pad' all match 'Notepad'. Use this BEFORE open_application when you aren't certain of an app's exact name, or after open_application fails - it tells you the exact installed name and resolved path so you don't have to guess. Returns a list of {name, path} matches; if there's more than one plausible match, ask the user which one they mean instead of picking for them. An EMPTY match list does not mean the app can't be opened - open_application has its own independent PATH/App Paths lookup and can succeed even when this index has nothing, so still try open_application with the user's own wording before telling them it couldn't be found.",
		"{\"type\":\"object\",\"properties\":{"
		"\"query\":{\"type\":\"string\",\"description\":\"app name or partial name to search for, e.g. 'blender' or 'kicad'\"}"
		"},\"required\":[\"query\"]}"});

	tools.push_back(ai::ToolDefinition{
		"list_open_windows",
		"List every currently open, visible window on this machine right now - title, owning process name, and PID - the same set Alt+Tab would show. Use this to check whether an app is already running/open before deciding to launch it again, to find the PID of something the user is referring to, or to answer questions like 'what's open right now' or 'is <app> running'. This is a live snapshot (re-checked every call), not the installed-apps index (find_application) or the connector list.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"open_application",
		"Open/launch a GUI application (e.g. Notepad, a browser, an image viewer) so the user can see and use it. Detached - does not wait for it to close and does not capture output. Use this instead of run_command for opening apps, since run_command waits for the process to exit and GUI apps don't exit on their own. path_or_name is resolved against the Windows App Paths registry, then PATH, then the installed-apps index (find_application's data source) as a fallback, then as a direct path. If you're not confident which exact app/path the user means (ambiguous name, multiple plausible matches from find_application, or a prior call failed to resolve it), ask the user to confirm rather than guessing.",
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

	tools.push_back(ai::ToolDefinition{
		"run_ffmpeg",
		"Run the ffmpeg CLI for media transcode/convert/clip/extract/thumbnail work - the binary is resolved automatically (PATH, then $DROIDCLI_FFMPEG_ROOT), you don't need to know where it lives. args is the raw ffmpeg argument string exactly as you'd type it after 'ffmpeg', e.g. '-y -i input.mp4 -vf scale=1280:-1 output.mp4' or '-i in.wav -ar 16000 out.wav'. Always pass -y to overwrite outputs without prompting, since there is no interactive terminal to answer that prompt. Runs synchronously and returns captured stdout/stderr/exit_code - encodes can take a while, so raise timeout_ms for large files (default 120000ms).",
		"{\"type\":\"object\",\"properties\":{"
		"\"args\":{\"type\":\"string\",\"description\":\"ffmpeg arguments, e.g. '-y -i input.mp4 output.webm'\"},"
		"\"work_dir\":{\"type\":\"string\",\"description\":\"optional working directory\"},"
		"\"timeout_ms\":{\"type\":\"integer\",\"description\":\"optional timeout in milliseconds, default 120000\"}"
		"},\"required\":[\"args\"]}"});

	tools.push_back(ai::ToolDefinition{
		"get_system_info",
		"Get the host machine droidcli is actually running on right now: OS name, OS version/build, CPU architecture, hostname, username, and current working directory. This was already queried once at startup and is in your system prompt, but call this if you need to double-check or report it precisely (e.g. the user asks 'what machine/OS is this'). Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

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
	if (tool_name == "find_application")
	{
		return find_applications_json(arguments_json);
	}
	if (tool_name == "list_open_windows")
	{
		return list_open_windows_json();
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
	if (tool_name == "get_system_info")
	{
		return build_system_info_json();
	}
	if (tool_name == "run_ffmpeg")
	{
		return run_ffmpeg_json(arguments_json);
	}

	return "{" + net::json_bool_field("ok", false) + ","
		+ net::json_string_field("error", "unknown tool: " + tool_name) + "}";
}

core::String DroidHost::build_agent_tools_json() const
{
	const core::Array<ai::ToolDefinition> tools = agent_tool_definitions();

	std::ostringstream stream;
	stream << "{\"tools\":[";
	for (size_t index = 0; index < tools.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const ai::ToolDefinition& tool = tools[index];
		stream << '{';
		stream << net::json_string_field("name", tool.name) << ',';
		stream << net::json_string_field("description", tool.description) << ',';
		// Embedded as a raw JSON schema object (not string-escaped) since
		// parameters_json_schema is already well-formed JSON - matches how
		// Ollama receives it in the "tools" request field.
		stream << "\"parameters\":" << (tool.parameters_json_schema.empty() ? "{}" : tool.parameters_json_schema);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

core::String DroidHost::agent_turn(const core::String& body)
{
	const core::String user_message = net::extract_json_string_field(body, "message");
	bool clear = false;
	net::extract_json_bool_field(body, "clear", clear);
	const core::String requested_session_id = net::extract_json_string_field(body, "session_id");

	core::String session_id;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (clear)
		{
			// Always starts a brand new session, ignoring any session_id in
			// the same request - see the field comment on agent_turn() in
			// host.hpp. Old history isn't deleted, just no longer active.
			agent_transcript_.clear();
			current_session_id_ = generate_session_id();
		}
		else if (!requested_session_id.empty() && requested_session_id != current_session_id_)
		{
			// Switching to (or resuming, possibly after a restart) a
			// different session: replay its persisted history into the
			// in-memory working transcript before this turn's message is
			// appended to it.
			agent_transcript_.clear();
			for (const MemoryRecord& record : memory_store_.load_session(requested_session_id))
			{
				agent_transcript_.push_back(ai::ChatMessage{ai::chat_role_from_string(record.role), record.content});
			}
			current_session_id_ = requested_session_id;
		}
		session_id = current_session_id_;
	}

	if (user_message.empty())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "missing message") + "}";
	}

	append_app_log("chat", "in", "user: " + user_message, true, session_id);

	if (!config_.enable_ai)
	{
		append_app_log("chat", "out", "rejected: AI is disabled (--no-ai)", false, session_id);
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "AI is disabled (--no-ai)") + "}";
	}

	ai::OllamaConfig ollama_config;
	bool seed_system_prompt = false;
	core::String system_prompt_text;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ollama_config.base_url = config_.ollama_url;
		ollama_config.model = config_.ollama_model;
		ollama_config.enabled = true;

		if (agent_transcript_.empty() && !config_.system_prompt.empty())
		{
			// Appends the concrete count from the startup scan (see initialize())
			// so the model is told a fact, not a capability description it might
			// discount - "you have 74 apps indexed" is harder to hedge on than
			// "you can look up installed apps".
			seed_system_prompt = true;
			system_prompt_text = config_.system_prompt
				+ " Right now this index has " + std::to_string(installed_apps_.size())
				+ " installed applications in it.";
		}
	}

	// record_agent_message() takes its own lock - must not be called while
	// mutex_ is already held (std::mutex isn't recursive), hence reading
	// the booleans/text above under lock and acting on them here instead.
	if (seed_system_prompt)
	{
		record_agent_message(session_id, ai::ChatRole::System, system_prompt_text);
	}
	record_agent_message(session_id, ai::ChatRole::User, user_message);

	const core::Array<ai::ToolDefinition> tools = agent_tool_definitions();

	// Coded against ai::ModelProvider, not ai::OllamaProvider directly, so a
	// second provider only means constructing a different concrete type
	// here - nothing below this line changes. See "Provider abstraction" in
	// ARCHITECTURE.md's extension plan and src/ai/model_provider.hpp.
	const ai::OllamaProvider ollama_provider(ollama_config);
	const ai::ModelProvider& provider = ollama_provider;

	return run_agent_tool_loop(session_id, tools, provider, 0, {}, 0, {});
}

core::String DroidHost::agent_tool_decision(const core::String& body)
{
	bool approved = false;
	net::extract_json_bool_field(body, "approved", approved);
	const core::String reason = net::extract_json_string_field(body, "reason");
	const core::String requested_session_id = net::extract_json_string_field(body, "session_id");

	core::String session_id;
	ai::ToolCall decided_call;
	core::Array<ai::ToolCall> tool_calls;
	size_t call_index = 0;
	int hop = 0;
	core::Array<PendingToolActionRecord> actions;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!pending_tool_call_.active
			|| (!requested_session_id.empty() && requested_session_id != pending_tool_call_.session_id))
		{
			return "{" + net::json_bool_field("ok", false) + ","
				+ net::json_string_field("error", "no tool call is awaiting a decision") + "}";
		}
		session_id = pending_tool_call_.session_id;
		tool_calls = pending_tool_call_.tool_calls;
		call_index = pending_tool_call_.call_index;
		hop = pending_tool_call_.hop;
		actions = pending_tool_call_.actions;
		decided_call = tool_calls[call_index];
		pending_tool_call_ = PendingAgentToolCall{};
	}

	const core::String tool_result = approved
		? execute_agent_tool(decided_call.name, decided_call.arguments_json)
		: "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", reason.empty() ? "user declined this action" : "user declined: " + reason) + "}";

	append_app_log("chat", "out",
		core::String(approved ? "tool " : "tool declined ") + decided_call.name + "(" + decided_call.arguments_json + ") -> " + tool_result,
		approved, session_id);
	record_agent_message(session_id, ai::ChatRole::Tool, tool_result);
	actions.push_back(PendingToolActionRecord{decided_call.name, decided_call.arguments_json, tool_result});

	ai::OllamaConfig ollama_config;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ollama_config.base_url = config_.ollama_url;
		ollama_config.model = config_.ollama_model;
		ollama_config.enabled = true;
	}
	const core::Array<ai::ToolDefinition> tools = agent_tool_definitions();
	const ai::OllamaProvider ollama_provider(ollama_config);
	const ai::ModelProvider& provider = ollama_provider;

	// resume_call_index is call_index + 1: the decision for tool_calls[call_index]
	// is already resolved above (executed or declined, recorded, appended to
	// actions) - run_agent_tool_loop's resume path continues with whatever
	// comes after it in this hop, or moves on to the next hop if that was the
	// last call.
	return run_agent_tool_loop(session_id, tools, provider, hop, tool_calls, call_index + 1, actions);
}

core::String DroidHost::run_agent_tool_loop(
	const core::String& session_id,
	const core::Array<ai::ToolDefinition>& tools,
	const ai::ModelProvider& provider,
	int hop,
	core::Array<ai::ToolCall> resume_calls,
	size_t resume_call_index,
	core::Array<PendingToolActionRecord> actions)
{
	constexpr int kMaxHops = 5;
	core::String final_assistant_text;
	bool budget_exhausted = false;

	// A non-empty resume_calls means we're continuing a hop that was already
	// fetched from the model before an earlier call in it paused for
	// approval - finish whatever's left in it before asking the model for
	// anything new. A fresh agent_turn() always passes an empty array here,
	// so this block is skipped entirely on a normal first call.
	if (!resume_calls.empty())
	{
		for (size_t call_index = resume_call_index; call_index < resume_calls.size(); ++call_index)
		{
			const ai::ToolCall& call = resume_calls[call_index];
			if (tool_call_requires_approval(call.name))
			{
				std::lock_guard<std::mutex> lock(mutex_);
				pending_tool_call_ = PendingAgentToolCall{true, session_id, hop, resume_calls, call_index, actions};
				return build_pending_tool_call_response(session_id, call, actions);
			}

			const core::String tool_result = execute_agent_tool(call.name, call.arguments_json);
			append_app_log("chat", "out",
				"tool " + call.name + "(" + call.arguments_json + ") -> " + tool_result, true, session_id);
			record_agent_message(session_id, ai::ChatRole::Tool, tool_result);
			actions.push_back(PendingToolActionRecord{call.name, call.arguments_json, tool_result});
		}

		if (hop == kMaxHops - 1)
		{
			budget_exhausted = true;
			append_app_log("chat", "out", "tool-call budget exhausted after " + std::to_string(kMaxHops) + " hops", false, session_id);
		}
		++hop;
	}

	for (; hop < kMaxHops; ++hop)
	{
		core::Array<ai::ChatMessage> transcript_copy;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			transcript_copy = agent_transcript_;
		}

		const ai::ProviderRequest request = provider.build_request(transcript_copy, tools);
		if (!request.valid)
		{
			append_app_log("chat", "out", "request build failed: " + request.error_message, false, session_id);
			return "{" + net::json_bool_field("ok", false) + ","
				+ net::json_string_field("error", request.error_message) + ","
				+ net::json_string_field("session_id", session_id) + "}";
		}

		int32_t status_code = 0;
		core::String response_body;
		const bool transport_ok = language_ai_transport_.post_json
			? language_ai_transport_.post_json(request.url, request.body, status_code, response_body)
			: false;

		const ai::ProviderResponse response = provider.parse_response(status_code, response_body, transport_ok);
		if (!response.transport_ok || !response.http_success)
		{
			const core::String error = response.error_message.empty() ? "Ollama request failed" : response.error_message;
			append_app_log("chat", "out", "hop " + std::to_string(hop) + " failed: " + error, false, session_id);
			// The user's message (and the system prompt, on a fresh session)
			// was already persisted via record_agent_message() above, even
			// though this turn is about to fail - include session_id so a
			// caller can still find it via GET /api/agent/history.
			return "{" + net::json_bool_field("ok", false) + ","
				+ net::json_string_field("error", error) + ","
				+ net::json_string_field("session_id", session_id) + "}";
		}

		record_agent_message(session_id, ai::ChatRole::Assistant, response.assistant_message);

		if (response.tool_calls.empty())
		{
			final_assistant_text = response.assistant_message;
			break;
		}

		bool paused = false;
		size_t call_index = 0;
		for (; call_index < response.tool_calls.size(); ++call_index)
		{
			const ai::ToolCall& call = response.tool_calls[call_index];
			if (tool_call_requires_approval(call.name))
			{
				std::lock_guard<std::mutex> lock(mutex_);
				pending_tool_call_ = PendingAgentToolCall{true, session_id, hop, response.tool_calls, call_index, actions};
				paused = true;
				break;
			}

			const core::String tool_result = execute_agent_tool(call.name, call.arguments_json);
			append_app_log("chat", "out",
				"tool " + call.name + "(" + call.arguments_json + ") -> " + tool_result, true, session_id);
			record_agent_message(session_id, ai::ChatRole::Tool, tool_result);
			actions.push_back(PendingToolActionRecord{call.name, call.arguments_json, tool_result});
		}

		if (paused)
		{
			return build_pending_tool_call_response(session_id, response.tool_calls[call_index], actions);
		}

		if (hop == kMaxHops - 1)
		{
			budget_exhausted = true;
			final_assistant_text = response.assistant_message;
			append_app_log("chat", "out", "tool-call budget exhausted after " + std::to_string(kMaxHops) + " hops", false, session_id);
		}
	}

	// A model can legitimately return an empty assistant_message with no
	// tool_calls (seen in practice with a local Ollama model on some
	// requests) - without this, the caller gets ok:true with a blank
	// "assistant" field and nothing to show the user, which reads as the
	// turn silently doing nothing rather than as an error. Substitute a
	// visible placeholder instead of ever returning a blank reply.
	if (final_assistant_text.empty())
	{
		final_assistant_text = budget_exhausted
			? "(reached the tool-call limit without a final reply - try rephrasing or breaking the request into smaller steps)"
			: "(no reply text - the model returned an empty response, try rephrasing)";
	}

	append_app_log("chat", "out", "assistant: " + final_assistant_text, true, session_id);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", true) << ',';
	stream << net::json_string_field("assistant", final_assistant_text) << ',';
	stream << net::json_string_field("session_id", session_id) << ',';
	if (budget_exhausted)
	{
		stream << net::json_bool_field("budget_exhausted", true) << ',';
	}
	stream << "\"actions\":[";
	for (size_t index = 0; index < actions.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const PendingToolActionRecord& action = actions[index];
		stream << '{';
		stream << net::json_string_field("tool", action.tool) << ',';
		stream << net::json_string_field("arguments_json", action.arguments_json) << ',';
		stream << net::json_string_field("result_json", action.result_json);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

core::String DroidHost::build_pending_tool_call_response(
	const core::String& session_id,
	const ai::ToolCall& call,
	const core::Array<PendingToolActionRecord>& actions_so_far)
{
	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", true) << ',';
	stream << net::json_string_field("session_id", session_id) << ',';
	stream << "\"pending_tool_call\":{";
	stream << net::json_string_field("tool", call.name) << ',';
	stream << net::json_string_field("arguments_json", call.arguments_json);
	stream << "},";
	stream << "\"actions\":[";
	for (size_t index = 0; index < actions_so_far.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const PendingToolActionRecord& action = actions_so_far[index];
		stream << '{';
		stream << net::json_string_field("tool", action.tool) << ',';
		stream << net::json_string_field("arguments_json", action.arguments_json) << ',';
		stream << net::json_string_field("result_json", action.result_json);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

void DroidHost::record_agent_message(const core::String& session_id, const ai::ChatRole role, const core::String& content)
{
	std::lock_guard<std::mutex> lock(mutex_);
	agent_transcript_.push_back(ai::ChatMessage{role, content});
	const int32_t hop_index = static_cast<int32_t>(agent_transcript_.size()) - 1;
	memory_store_.append(session_id, hop_index, ai::chat_role_to_string(role), content);
}

core::String DroidHost::build_agent_history_json(const core::String& session_id) const
{
	core::String resolved_session_id;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		resolved_session_id = session_id.empty() ? current_session_id_ : session_id;
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_string_field("session_id", resolved_session_id) << ',';
	stream << "\"messages\":[";
	bool first = true;
	for (const MemoryRecord& record : memory_store_.load_session(resolved_session_id))
	{
		if (!first)
		{
			stream << ',';
		}
		first = false;
		stream << '{';
		stream << "\"hop_index\":" << record.hop_index << ',';
		stream << net::json_string_field("role", record.role) << ',';
		stream << net::json_string_field("content", record.content) << ',';
		stream << net::json_string_field("created_at", record.created_at);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

core::String DroidHost::build_agent_sessions_json() const
{
	core::String current;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		current = current_session_id_;
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_string_field("current_session_id", current) << ',';
	stream << "\"session_ids\":[";
	bool first = true;
	for (const core::String& id : memory_store_.list_session_ids())
	{
		if (!first)
		{
			stream << ',';
		}
		first = false;
		stream << '"' << net::escape_json_string(id) << '"';
	}
	stream << "]}";
	return stream.str();
}

void DroidHost::log_thread_event(const core::String& thread_name, const core::String& event)
{
	// A "threw: ..." event is the one case worth flagging as a failure in
	// the log (success:false) - "spawned"/"joined" are routine lifecycle,
	// not something to alarm on.
	const bool success = event.rfind("threw", 0) != 0;
	append_app_log("thread", thread_name, event, success);
}

} // namespace droidcli::cli
