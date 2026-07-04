#include "metaagent_host.hpp"

#include "tools/sync_http_client.hpp"

#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace metaagent::app_host {
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

// Minimal RFC-4180-ish CSV parser: handles quoted fields with embedded commas,
// newlines, and "" escaped quotes. Returns rows of fields (row 0 = header).
std::vector<std::vector<core::String>> parse_csv(const core::String& content)
{
	std::vector<std::vector<core::String>> rows;
	std::vector<core::String> row;
	core::String field;
	bool in_quotes = false;

	for (size_t i = 0; i < content.size(); ++i)
	{
		const char c = content[i];
		if (in_quotes)
		{
			if (c == '"')
			{
				if (i + 1 < content.size() && content[i + 1] == '"')
				{
					field.push_back('"');
					++i;
				}
				else
				{
					in_quotes = false;
				}
			}
			else
			{
				field.push_back(c);
			}
		}
		else if (c == '"')
		{
			in_quotes = true;
		}
		else if (c == ',')
		{
			row.push_back(field);
			field.clear();
		}
		else if (c == '\n')
		{
			row.push_back(field);
			field.clear();
			rows.push_back(row);
			row.clear();
		}
		else if (c != '\r')
		{
			field.push_back(c);
		}
	}
	if (!field.empty() || !row.empty())
	{
		row.push_back(field);
		rows.push_back(row);
	}
	return rows;
}

int csv_column_index(const std::vector<core::String>& header, const char* name)
{
	for (size_t i = 0; i < header.size(); ++i)
	{
		if (header[i] == name)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

core::String csv_field(const std::vector<core::String>& row, int index)
{
	if (index < 0 || static_cast<size_t>(index) >= row.size())
	{
		return core::String {};
	}
	return row[index];
}

core::String preview_text(const core::String& text, size_t max_chars)
{
	if (text.size() <= max_chars)
	{
		return text;
	}
	return text.substr(0, max_chars) + "...";
}

core::String read_text_file(const core::String& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		return core::String {};
	}
	std::ostringstream stream;
	stream << file.rdbuf();
	return stream.str();
}

// Strip any directory prefix (CSV image is e.g. "Release_1_PNG\\foo.png", while
// the media player reports the bare "foo.png").
core::String base_filename(const core::String& path)
{
	const size_t pos = path.find_last_of("/\\");
	return pos == core::String::npos ? path : path.substr(pos + 1);
}

} // namespace

void MetaAgentHost::configure(const HostConfig& config)
{
	std::lock_guard<std::mutex> lock(mutex_);
	config_ = config;
}

void MetaAgentHost::initialize()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);

		metaagent::initialize_defaults();

		session_.active = true;
		session_.map_name = "metaagent_app";
		session_.build_label = "desktop";
		session_.http_enabled = true;
		session_.http_router_bound = true;
		session_.features.input = true;
		session_.features.camera = true;
		session_.features.networking = true;
		session_.features.ui = true;
		session_.features.particle = true;
		session_.features.ai = config_.enable_ai;
		session_.features.recording = true;

		// Particles are a UE-plugin runtime; the desktop host does not run a
		// ParticleScheduler. The session keeps the particle feature flag so the
		// (ue5-scoped) particle runtime reports correctly when UE5 runtimes are on.
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

	append_app_log("host", "event", "MetaAgentHost initialized", true);
	media_dataset_mirror_.set_preferred_data_directory(config_.media_data_directory);
}

void MetaAgentHost::wire_callbacks()
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

void MetaAgentHost::tick(float /*delta_seconds*/)
{
	// No particle simulation runs in the desktop host; particles are driven by
	// the Unreal Engine plugin. Kept as a stub so the host tick timer is harmless.
}

session::RuntimeSession& MetaAgentHost::session()
{
	return session_;
}

const session::RuntimeSession& MetaAgentHost::session() const
{
	return session_;
}

net::HandlerContext MetaAgentHost::make_handler_context()
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

net::RouteDispatchResult MetaAgentHost::dispatch_route(const net::HttpRequest& request)
{
	std::lock_guard<std::mutex> lock(mutex_);
	net::RouteDispatchResult result = routes_.dispatch(request, make_handler_context());
	if (result.notify.has_notify_message)
	{
		on_notify(result.notify.notify_message.text);
	}
	return result;
}

void MetaAgentHost::on_notify(const core::String& message)
{
	notify_log_.push_back(message);
	if (notify_log_.size() > 64)
	{
		notify_log_.erase(notify_log_.begin());
	}
	std::cout << "[notify] " << message << std::endl;
}

core::String MetaAgentHost::build_notify_log_json() const
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

core::String MetaAgentHost::build_status_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	const runtime::RecordingSnapshot recording = runtime::invoke_query_recording(host_services_);
	const runtime::AiSnapshot ai = runtime::invoke_query_ai(host_services_);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_string_field("host", "metaagent_app") << ',';
	stream << net::json_string_field("map", session_.map_name) << ',';
	stream << net::json_string_field("build", session_.build_label) << ',';
	stream << net::json_bool_field("active", session_.active) << ',';
	stream << net::json_bool_field("recording", recording.capture_active) << ',';
	stream << net::json_bool_field("autopilot", ai.autopilot_enabled) << ',';
	stream << net::json_bool_field("cinematic", cinematic_enabled_) << ',';
	stream << net::json_bool_field("focus_particles", focus_particles_);
	stream << '}';
	return stream.str();
}

core::String MetaAgentHost::build_gui_catalog_json() const
{
	const app::GuiPanelCatalog catalog = app::build_gui_panel_catalog();
	std::ostringstream stream;
	stream << "{\"sections\":[";
	for (size_t section_index = 0; section_index < catalog.sections.size(); ++section_index)
	{
		if (section_index > 0)
		{
			stream << ',';
		}
		const app::GuiPanelSection& section = catalog.sections[section_index];
		stream << '{';
		stream << net::json_string_field("section_id", section.section_id) << ',';
		stream << net::json_string_field("title", section.title) << ',';
		stream << net::json_bool_field("always_on", section.always_on) << ',';
		stream << "\"rows\":[";
		for (size_t row_index = 0; row_index < section.rows.size(); ++row_index)
		{
			if (row_index > 0)
			{
				stream << ',';
			}
			const app::GuiPanelRow& row = section.rows[row_index];
			stream << '{';
			stream << net::json_string_field("action_id", row.action_id) << ',';
			stream << net::json_string_field("key_label", row.key_label) << ',';
			stream << net::json_string_field("description", row.description);
			stream << '}';
		}
		stream << "]}";
	}
	stream << "]}";
	return stream.str();
}

core::String MetaAgentHost::build_config_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ai_enabled", config_.enable_ai) << ',';
	stream << net::json_string_field("ollama_url", config_.ollama_url) << ',';
	stream << net::json_string_field("ollama_model", config_.ollama_model) << ',';
	stream << net::json_string_field("media_player_base_url", config_.media_player_base_url) << ',';
	stream << net::json_string_field("adapter_url", config_.adapter_url) << ',';
	stream << net::json_string_field("media_player_project_dir", config_.media_player_project_dir) << ',';
	stream << net::json_string_field("media_player_build_command", config_.media_player_build_command) << ',';
	stream << net::json_string_field("media_player_run_command", config_.media_player_run_command) << ',';
	stream << net::json_string_field("adapter_project_dir", config_.adapter_project_dir) << ',';
	stream << net::json_string_field("adapter_launch_command", config_.adapter_launch_command) << ',';
	stream << net::json_string_field("dataset_output_dir", config_.dataset_output_dir);
	stream << '}';
	return stream.str();
}

void MetaAgentHost::apply_command_side_effects(const app::CommandId command)
{
	switch (command)
	{
	// Pattern stepping is a particle command handled by the UE plugin; the
	// desktop host has no scheduler, so these are intentionally no-ops here.
	case app::CommandId::PatternStepForward:
	case app::CommandId::PatternStepBackward:
		break;
	case app::CommandId::ToggleRecording:
		runtime::invoke_toggle_recording(host_services_);
		break;
	case app::CommandId::ToggleAutopilot:
		runtime::invoke_toggle_autopilot(host_services_);
		break;
	case app::CommandId::ToggleCinematicCamera:
		cinematic_enabled_ = !cinematic_enabled_;
		break;
	case app::CommandId::ToggleFocusParticles:
		focus_particles_ = !focus_particles_;
		break;
	case app::CommandId::CycleCinematicStyle:
		break;
	case app::CommandId::ToggleNetworkingRuntime:
		session_.features.networking = !session_.features.networking;
		session_.http_enabled = session_.features.networking;
		break;
	default:
		break;
	}
}

core::String MetaAgentHost::dispatch_command(const core::String& command_name)
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

core::String MetaAgentHost::build_media_player_url(const core::String& path) const
{
	core::String base = config_.media_player_base_url;
	while (!base.empty() && base.back() == '/')
	{
		base.pop_back();
	}

	if (path.empty())
	{
		return base;
	}

	if (path.front() == '/')
	{
		return base + path;
	}

	return base + "/" + path;
}

void MetaAgentHost::append_media_control_log(
	const core::String& action,
	const core::String& summary,
	const bool success,
	const core::String& direction)
{
	std::lock_guard<std::mutex> lock(mutex_);
	MediaControlLogEntry entry;
	entry.timestamp = make_log_timestamp();
	entry.channel = "media";
	entry.direction = direction;
	entry.action = action;
	entry.summary = summary;
	entry.success = success;
	media_control_log_.push_back(entry);
	if (media_control_log_.size() > 256)
	{
		media_control_log_.erase(media_control_log_.begin());
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

void MetaAgentHost::append_app_log(
	const core::String& channel,
	const core::String& direction,
	const core::String& summary,
	const bool success)
{
	std::lock_guard<std::mutex> lock(mutex_);
	MediaControlLogEntry entry;
	entry.timestamp = make_log_timestamp();
	entry.channel = channel;
	entry.direction = direction;
	entry.action = channel;
	entry.summary = summary;
	entry.success = success;
	media_control_log_.push_back(entry);
	if (media_control_log_.size() > 256)
	{
		media_control_log_.erase(media_control_log_.begin());
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

core::String MetaAgentHost::make_log_timestamp()
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

bool MetaAgentHost::should_emit_periodic_log(
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

core::String MetaAgentHost::proxy_media_player_get(const core::String& path) const
{
	MetaAgentHost* self = const_cast<MetaAgentHost*>(this);
	const std::time_t now_utc = std::time(nullptr);
	if (path == "/api/status")
	{
		if (should_emit_periodic_log(now_utc, self->last_status_out_log_utc_, 15))
		{
			self->append_app_log("http", "out", "GET " + path, true);
		}
	}
	else
	{
		self->append_app_log("http", "out", "GET " + path, true);
	}
	const bool is_clip_list_request = path == "/api/clips";

	core::String base_url;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		base_url = config_.media_player_base_url;
	}

	if (base_url.empty())
	{
		self->append_app_log("http", "in", "GET failed: media player URL not configured", false);
		return "{"
			+ net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "media player URL not configured")
			+ "}";
	}

	core::String base = base_url;
	while (!base.empty() && base.back() == '/')
	{
		base.pop_back();
	}
	const core::String url = path.empty()
		? base
		: (path.front() == '/' ? base + path : base + "/" + path);
	int32_t status_code = 0;
	core::String response_body;
	const bool transport_ok = tools::sync_http_get(url, status_code, response_body);
	if (!transport_ok)
	{
		self->media_player_online_last_seen_ = false;

		if (is_clip_list_request)
		{
			const core::String fallback = self->media_dataset_mirror_.build_clips_json_fallback();
			if (fallback != "[]")
			{
				if (should_emit_periodic_log(now_utc, self->last_clips_fallback_log_utc_, 15))
				{
					self->append_app_log("http", "in", "GET /api/clips -> fallback dataset", true);
				}
				return fallback;
			}
		}

		if (path != "/api/status"
			|| should_emit_periodic_log(now_utc, self->last_status_fail_log_utc_, 15))
		{
			self->append_app_log("http", "in", "GET failed: media player unreachable", false);
		}
		return "{"
			+ net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "media player unreachable") + ","
			+ net::json_string_field("media_player_url", base_url)
			+ "}";
	}

	const bool success = status_code >= 200 && status_code < 300;
	if (!self->media_player_online_last_seen_ && success)
	{
		self->append_app_log("http", "in", "GET recovered: media player reachable", true);
	}
	self->media_player_online_last_seen_ = success;

	if (path != "/api/status"
		|| should_emit_periodic_log(now_utc, self->last_status_out_log_utc_, 15))
	{
		self->append_app_log("http", "in", "GET " + path + " -> " + std::to_string(status_code), success);
	}
	if (is_clip_list_request && success)
	{
		self->media_dataset_mirror_.remember_remote_clips_json(response_body);
	}

	if (response_body.empty())
	{
		response_body = "{}";
	}

	return response_body;
}

core::String MetaAgentHost::proxy_media_player_post(const core::String& path, const core::String& body)
{
	append_app_log("http", "out", "POST " + path, true);

	core::String base_url;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		base_url = config_.media_player_base_url;
	}

	if (base_url.empty())
	{
		const core::String response = "{"
			+ net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "media player URL not configured")
			+ "}";
		append_media_control_log(path, "media player URL not configured", false, "in");
		return response;
	}

	core::String base = base_url;
	while (!base.empty() && base.back() == '/')
	{
		base.pop_back();
	}
	const core::String url = path.empty()
		? base
		: (path.front() == '/' ? base + path : base + "/" + path);
	int32_t status_code = 0;
	core::String response_body;
	const bool transport_ok = tools::sync_http_post_json(url, body, status_code, response_body);
	if (!transport_ok)
	{
		const core::String response = "{"
			+ net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "media player unreachable") + ","
			+ net::json_string_field("media_player_url", base_url)
			+ "}";
		append_media_control_log(path, "media player unreachable", false, "in");
		return response;
	}

	if (response_body.empty())
	{
		response_body = "{}";
	}

	const bool success = status_code >= 200 && status_code < 300;
	append_media_control_log(path, "POST " + path + " -> " + std::to_string(status_code), success, "in");
	if (!response_body.empty())
	{
		append_media_control_log(path, response_body, success, "in");
	}
	return response_body;
}

void MetaAgentHost::ensure_summaries_loaded()
{
	core::String dir;
	bool need_load = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		dir = config_.dataset_output_dir;
		need_load = !summaries_loaded_ || summaries_loaded_for_dir_ != dir;
	}
	if (!need_load)
	{
		return;
	}

	// Build the map outside the lock; parsing the CSV can take a moment.
	std::map<core::String, core::String> map;
	if (!dir.empty())
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		core::String summaries_path;
		if (fs::is_directory(dir, ec))
		{
			for (const auto& entry : fs::directory_iterator(dir, ec))
			{
				if (!entry.is_regular_file())
				{
					continue;
				}
				const core::String name = entry.path().filename().string();
				const core::String suffix = "_SUMMARIES.csv";
				if (name.size() >= suffix.size()
					&& name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
				{
					summaries_path = entry.path().string();
					break;
				}
			}
		}

		if (!summaries_path.empty())
		{
			const auto rows = parse_csv(read_text_file(summaries_path));
			if (!rows.empty())
			{
				const int ci_image = csv_column_index(rows[0], "image");
				const int ci_summary = csv_column_index(rows[0], "summary");
				for (size_t r = 1; r < rows.size(); ++r)
				{
					const core::String image = csv_field(rows[r], ci_image);
					const core::String summary = csv_field(rows[r], ci_summary);
					if (!image.empty() && !summary.empty())
					{
						map[base_filename(image)] = summary;
					}
				}
			}
		}
	}

	std::lock_guard<std::mutex> lock(mutex_);
	summary_by_basename_ = std::move(map);
	summaries_loaded_ = true;
	summaries_loaded_for_dir_ = dir;
}

core::String MetaAgentHost::condense_summary(const core::String& summary)
{
	core::String url;
	core::String model;
	bool enabled = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		url = config_.ollama_url;
		model = config_.ollama_model;
		enabled = config_.enable_ai;
	}

	if (!enabled || url.empty() || model.empty() || summary.empty())
	{
		return summary;
	}

	while (!url.empty() && url.back() == '/')
	{
		url.pop_back();
	}

	const core::String system_prompt =
		"You rewrite document summaries into a single short subtitle. "
		"Drop filler openings such as 'This document outlines', 'This page details', "
		"'This document is', 'This routing sheet', 'The document'. Keep only the essential "
		"facts (who/what/when/where). Reply with one concise sentence and nothing else - "
		"no preamble, no quotes, no label.";

	const core::String body = core::String("{")
		+ "\"model\":\"" + net::escape_json_string(model) + "\","
		+ "\"stream\":false,"
		+ "\"messages\":["
			+ "{\"role\":\"system\",\"content\":\"" + net::escape_json_string(system_prompt) + "\"},"
			+ "{\"role\":\"user\",\"content\":\"" + net::escape_json_string(summary) + "\"}"
		+ "]}";

	int32_t status_code = 0;
	core::String response_body;
	const bool ok = tools::sync_http_post_json(url + "/api/chat", body, status_code, response_body);
	if (!ok || status_code < 200 || status_code >= 300)
	{
		return summary;
	}

	core::String condensed = net::extract_json_string_field(response_body, "content");

	// Trim whitespace and any surrounding quotes the model may have added.
	const auto trim = [](core::String text)
	{
		const size_t start = text.find_first_not_of(" \t\r\n\"'");
		if (start == core::String::npos)
		{
			return core::String {};
		}
		const size_t end = text.find_last_not_of(" \t\r\n\"'");
		return text.substr(start, end - start + 1);
	};
	condensed = trim(condensed);

	return condensed.empty() ? summary : condensed;
}

void MetaAgentHost::push_subtitle_for_clip(const core::String& clip_name)
{
	ensure_summaries_loaded();

	const core::String key = base_filename(clip_name);
	core::String summary;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto it = summary_by_basename_.find(key);
		if (it != summary_by_basename_.end())
		{
			summary = it->second;
		}
	}

	if (summary.empty())
	{
		append_app_log("media", "out", "no summary for " + key + " (subtitle left as-is)", false);
		return;
	}

	// Use the cached condensed subtitle if we already rewrote this clip.
	core::String text;
	bool cached = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto it = condensed_by_basename_.find(key);
		if (it != condensed_by_basename_.end())
		{
			text = it->second;
			cached = true;
		}
	}

	if (!cached)
	{
		text = condense_summary(summary);
		std::lock_guard<std::mutex> lock(mutex_);
		condensed_by_basename_[key] = text;
	}

	const core::String subtitle_body = "{"
		+ net::json_string_field("text", text) + ","
		+ net::json_bool_field("enabled", true)
		+ "}";
	proxy_media_player_post("/api/subtitles", subtitle_body);
}

core::String MetaAgentHost::media_navigate(const core::String& media_path, const core::String& body)
{
	const core::String response = proxy_media_player_post(media_path, body);
	const core::String clip_name = net::extract_json_string_field(response, "clipName");
	if (!clip_name.empty())
	{
		push_subtitle_for_clip(clip_name);
	}
	return response;
}

core::String MetaAgentHost::sync_media_subtitle()
{
	const core::String status = proxy_media_player_get("/api/status");
	const core::String clip_name = net::extract_json_string_field(status, "clipName");
	if (clip_name.empty())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "no current clip") + "}";
	}
	push_subtitle_for_clip(clip_name);
	return "{" + net::json_bool_field("ok", true) + ","
		+ net::json_string_field("clip", clip_name) + "}";
}

core::String MetaAgentHost::build_media_control_log_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream stream;
	stream << "{\"entries\":[";
	for (size_t index = 0; index < media_control_log_.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const MediaControlLogEntry& entry = media_control_log_[index];
		stream << '{';
		stream << net::json_string_field("timestamp", entry.timestamp) << ',';
		stream << net::json_string_field("channel", entry.channel) << ',';
		stream << net::json_string_field("direction", entry.direction) << ',';
		stream << net::json_string_field("action", entry.action) << ',';
		stream << net::json_string_field("summary", entry.summary) << ',';
		stream << net::json_bool_field("success", entry.success);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

core::String MetaAgentHost::build_app_log_json() const
{
	return build_media_control_log_json();
}

core::String MetaAgentHost::build_network_status_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);

	bool media_player_online = false;
	if (!config_.media_player_base_url.empty())
	{
		const core::String url = build_media_player_url("/api/health");
		int32_t status_code = 0;
		core::String response_body;
		const bool transport_ok = tools::sync_http_get(url, status_code, response_body);
		media_player_online = transport_ok && status_code >= 200 && status_code < 300;
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("networking_enabled", session_.features.networking) << ',';
	stream << net::json_bool_field("media_player_online", media_player_online) << ',';
	stream << net::json_string_field("media_player_url", config_.media_player_base_url);
	stream << '}';
	return stream.str();
}

core::String MetaAgentHost::build_runtime_catalog_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return app::build_runtime_catalog_json(session_, ue5_runtimes_enabled_);
}

core::String MetaAgentHost::build_ollama_status_json()
{
	std::lock_guard<std::mutex> lock(mutex_);

	core::String tags_url = config_.ollama_url;
	while (!tags_url.empty() && tags_url.back() == '/')
	{
		tags_url.pop_back();
	}
	tags_url += "/api/tags";

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

core::String MetaAgentHost::update_ollama_config(const core::String& body)
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
	condensed_by_basename_.clear();
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

core::String MetaAgentHost::update_config(const core::String& body)
{
	std::lock_guard<std::mutex> lock(mutex_);

	const core::String ollama_url = net::extract_json_string_field(body, "ollama_url");
	if (!ollama_url.empty())
	{
		config_.ollama_url = ollama_url;
		condensed_by_basename_.clear();
	}

	const core::String ollama_model = net::extract_json_string_field(body, "ollama_model");
	if (!ollama_model.empty())
	{
		config_.ollama_model = ollama_model;
		condensed_by_basename_.clear();
	}

	const core::String media_player_base_url = net::extract_json_string_field(body, "media_player_base_url");
	if (!media_player_base_url.empty())
	{
		config_.media_player_base_url = media_player_base_url;
	}

	const core::String adapter_url = net::extract_json_string_field(body, "adapter_url");
	if (!adapter_url.empty())
	{
		config_.adapter_url = adapter_url;
	}

	const core::String media_player_project_dir = net::extract_json_string_field(body, "media_player_project_dir");
	if (!media_player_project_dir.empty())
	{
		config_.media_player_project_dir = media_player_project_dir;
	}

	const core::String media_player_build_command = net::extract_json_string_field(body, "media_player_build_command");
	if (!media_player_build_command.empty())
	{
		config_.media_player_build_command = media_player_build_command;
	}

	const core::String media_player_run_command = net::extract_json_string_field(body, "media_player_run_command");
	if (!media_player_run_command.empty())
	{
		config_.media_player_run_command = media_player_run_command;
	}

	const core::String adapter_project_dir = net::extract_json_string_field(body, "adapter_project_dir");
	if (!adapter_project_dir.empty())
	{
		config_.adapter_project_dir = adapter_project_dir;
	}

	const core::String adapter_launch_command = net::extract_json_string_field(body, "adapter_launch_command");
	if (!adapter_launch_command.empty())
	{
		config_.adapter_launch_command = adapter_launch_command;
	}

	const core::String dataset_output_dir = net::extract_json_string_field(body, "dataset_output_dir");
	if (!dataset_output_dir.empty())
	{
		config_.dataset_output_dir = dataset_output_dir;
		// Force the subtitle summary map to reload from the new dir.
		summaries_loaded_ = false;
		condensed_by_basename_.clear();
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
	stream << net::json_string_field("ollama_model", config_.ollama_model) << ',';
	stream << net::json_string_field("media_player_base_url", config_.media_player_base_url) << ',';
	stream << net::json_string_field("adapter_url", config_.adapter_url) << ',';
	stream << net::json_string_field("media_player_project_dir", config_.media_player_project_dir) << ',';
	stream << net::json_string_field("media_player_build_command", config_.media_player_build_command) << ',';
	stream << net::json_string_field("media_player_run_command", config_.media_player_run_command) << ',';
	stream << net::json_string_field("adapter_project_dir", config_.adapter_project_dir) << ',';
	stream << net::json_string_field("adapter_launch_command", config_.adapter_launch_command) << ',';
	stream << net::json_string_field("dataset_output_dir", config_.dataset_output_dir);
	stream << '}';
	return stream.str();
}

core::String MetaAgentHost::build_adapter_status_json()
{
	core::String base;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		base = config_.adapter_url;
	}
	while (!base.empty() && base.back() == '/')
	{
		base.pop_back();
	}

	bool online = false;
	core::String device;
	core::String mode;
	core::String dtype;
	if (!base.empty())
	{
		int32_t status_code = 0;
		core::String response_body;
		const bool transport_ok = tools::sync_http_get(base + "/api/health", status_code, response_body);
		online = transport_ok && status_code >= 200 && status_code < 300;
		if (online)
		{
			device = net::extract_json_string_field(response_body, "device");

			int32_t info_status = 0;
			core::String info_body;
			if (tools::sync_http_get(base + "/api/model-info", info_status, info_body)
				&& info_status >= 200 && info_status < 300)
			{
				mode = net::extract_json_string_field(info_body, "mode");
				dtype = net::extract_json_string_field(info_body, "dtype");
				if (device.empty())
				{
					device = net::extract_json_string_field(info_body, "device");
				}
			}
		}
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("online", online) << ',';
	stream << net::json_string_field("adapter_url", base) << ',';
	stream << net::json_string_field("device", device) << ',';
	stream << net::json_string_field("mode", mode) << ',';
	stream << net::json_string_field("dtype", dtype);
	stream << '}';
	return stream.str();
}

core::String MetaAgentHost::proxy_adapter_summarize(const core::String& body)
{
	append_app_log("adapter", "out", "POST /api/summarize", true);

	core::String base;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		base = config_.adapter_url;
	}

	if (base.empty())
	{
		append_app_log("adapter", "in", "adapter URL not configured", false);
		return "{"
			+ net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "adapter URL not configured")
			+ "}";
	}

	while (!base.empty() && base.back() == '/')
	{
		base.pop_back();
	}

	const core::String url = base + "/api/summarize";
	int32_t status_code = 0;
	core::String response_body;
	const bool transport_ok = tools::sync_http_post_json(url, body, status_code, response_body);
	if (!transport_ok)
	{
		append_app_log("adapter", "in", "adapter unreachable", false);
		return "{"
			+ net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "adapter unreachable") + ","
			+ net::json_string_field("adapter_url", base)
			+ "}";
	}

	if (response_body.empty())
	{
		response_body = "{}";
	}

	const bool success = status_code >= 200 && status_code < 300;
	append_app_log("adapter", "in", "POST /api/summarize -> " + std::to_string(status_code), success);
	return response_body;
}

core::String MetaAgentHost::build_process_status_json()
{
	core::String media_dir;
	core::String adapter_dir;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		media_dir = config_.media_player_project_dir;
		adapter_dir = config_.adapter_project_dir;
	}

	const core::Array<ProcessInfo> processes = process_manager_.snapshot();

	std::ostringstream stream;
	stream << '{';
	stream << net::json_string_field("media_player_dir", media_dir) << ',';
	stream << net::json_string_field("adapter_dir", adapter_dir) << ',';
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

core::String MetaAgentHost::build_dataset_json()
{
	core::String dir;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		dir = config_.dataset_output_dir;
	}

	if (dir.empty())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "dataset output dir not configured") + "}";
	}

	namespace fs = std::filesystem;
	std::error_code ec;
	if (!fs::is_directory(dir, ec))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "dataset dir not found") + ","
			+ net::json_string_field("dataset_dir", dir) + "}";
	}

	// Find the corpus CSVs by suffix; the dataset prefix (e.g. Release_1_) varies.
	core::String ocr_path;
	core::String summaries_path;
	core::String objs_path;
	for (const auto& entry : fs::directory_iterator(dir, ec))
	{
		if (!entry.is_regular_file())
		{
			continue;
		}
		const core::String name = entry.path().filename().string();
		auto ends_with = [&name](const char* suffix)
		{
			const core::String suf = suffix;
			return name.size() >= suf.size()
				&& name.compare(name.size() - suf.size(), suf.size(), suf) == 0;
		};
		if (ocr_path.empty() && ends_with("_OCR.csv"))
		{
			ocr_path = entry.path().string();
		}
		else if (summaries_path.empty() && ends_with("_SUMMARIES.csv"))
		{
			summaries_path = entry.path().string();
		}
		else if (objs_path.empty() && ends_with("_OBJS.csv"))
		{
			objs_path = entry.path().string();
		}
	}

	// image -> summary
	std::map<core::String, core::String> summary_by_image;
	if (!summaries_path.empty())
	{
		const auto rows = parse_csv(read_text_file(summaries_path));
		if (!rows.empty())
		{
			const int ci_image = csv_column_index(rows[0], "image");
			const int ci_summary = csv_column_index(rows[0], "summary");
			for (size_t r = 1; r < rows.size(); ++r)
			{
				const core::String image = csv_field(rows[r], ci_image);
				if (!image.empty())
				{
					summary_by_image[image] = csv_field(rows[r], ci_summary);
				}
			}
		}
	}

	// image -> detected object count
	std::map<core::String, int> objects_by_image;
	if (!objs_path.empty())
	{
		const auto rows = parse_csv(read_text_file(objs_path));
		if (!rows.empty())
		{
			const int ci_image = csv_column_index(rows[0], "image");
			for (size_t r = 1; r < rows.size(); ++r)
			{
				const core::String image = csv_field(rows[r], ci_image);
				if (!image.empty())
				{
					objects_by_image[image]++;
				}
			}
		}
	}

	std::ostringstream entries;
	bool first = true;
	size_t total = 0;
	size_t emitted = 0;
	constexpr size_t kMaxRows = 300;

	if (!ocr_path.empty())
	{
		const auto rows = parse_csv(read_text_file(ocr_path));
		if (!rows.empty())
		{
			const int ci_image = csv_column_index(rows[0], "image");
			const int ci_status = csv_column_index(rows[0], "status");
			const int ci_text = csv_column_index(rows[0], "text");
			for (size_t r = 1; r < rows.size(); ++r)
			{
				const core::String image = csv_field(rows[r], ci_image);
				if (image.empty())
				{
					continue;
				}
				++total;
				if (emitted >= kMaxRows)
				{
					continue;
				}

				core::String summary;
				const auto summary_it = summary_by_image.find(image);
				if (summary_it != summary_by_image.end())
				{
					summary = summary_it->second;
				}
				int object_count = 0;
				const auto objects_it = objects_by_image.find(image);
				if (objects_it != objects_by_image.end())
				{
					object_count = objects_it->second;
				}

				if (!first)
				{
					entries << ',';
				}
				first = false;
				entries << '{';
				entries << net::json_string_field("image", image) << ',';
				entries << net::json_string_field("status", csv_field(rows[r], ci_status)) << ',';
				entries << net::json_string_field("ocr_preview", preview_text(csv_field(rows[r], ci_text), 200)) << ',';
				entries << net::json_string_field("summary_preview", preview_text(summary, 200)) << ',';
				entries << "\"objects\":" << object_count;
				entries << '}';
				++emitted;
			}
		}
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", true) << ',';
	stream << net::json_string_field("dataset_dir", dir) << ',';
	stream << net::json_string_field("ocr_csv", ocr_path) << ',';
	stream << net::json_string_field("summaries_csv", summaries_path) << ',';
	stream << net::json_string_field("objs_csv", objs_path) << ',';
	stream << "\"count\":" << total << ',';
	stream << "\"shown\":" << emitted << ',';
	stream << "\"entries\":[" << entries.str() << "]";
	stream << '}';
	return stream.str();
}

core::String MetaAgentHost::build_media_player()
{
	core::String dir;
	core::String command;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		dir = config_.media_player_project_dir;
		command = config_.media_player_build_command;
	}

	if (dir.empty())
	{
		append_app_log("process", "in", "media player project dir not configured", false);
		return build_process_status_json();
	}

	core::String error;
	const bool ok = process_manager_.launch("media_build", "Media Player build", dir, command, error);
	append_app_log("process", "out", ok ? ("build started: " + command) : ("build failed: " + error), ok);
	return build_process_status_json();
}

core::String MetaAgentHost::run_media_player()
{
	core::String dir;
	core::String command;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		dir = config_.media_player_project_dir;
		command = config_.media_player_run_command;
	}

	if (dir.empty())
	{
		append_app_log("process", "in", "media player project dir not configured", false);
		return build_process_status_json();
	}

	// Dev layout: the OF project dir contains bin/ and the binary runs from there.
	// Dist layout: media-player/ is the staged bin content itself (exe at top).
	core::String run_dir = dir;
	while (!run_dir.empty() && (run_dir.back() == '/' || run_dir.back() == '\\'))
	{
		run_dir.pop_back();
	}
	{
		std::error_code ec;
		if (std::filesystem::is_directory(run_dir + "/bin", ec))
		{
			run_dir += "/bin";
		}
	}

	core::String error;
	const bool ok = process_manager_.launch("media_run", "Media Player (RunRelease)", run_dir, command, error);
	append_app_log("process", "out", ok ? ("run started: " + command) : ("run failed: " + error), ok);
	return build_process_status_json();
}

core::String MetaAgentHost::stop_media_player_process()
{
	core::String error;
	const bool ok = process_manager_.stop("media_run", error);
	append_app_log("process", "out", ok ? "media player process stopped" : ("stop failed: " + error), ok);
	return build_process_status_json();
}

core::String MetaAgentHost::launch_adapter_server()
{
	core::String dir;
	core::String command;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		dir = config_.adapter_project_dir;
		command = config_.adapter_launch_command;
	}

	if (dir.empty())
	{
		append_app_log("process", "in", "adapter project dir not configured", false);
		return build_process_status_json();
	}

	core::String error;
	const bool ok = process_manager_.launch("adapter_server", "Adapter server (uv)", dir, command, error);
	append_app_log("process", "out", ok ? ("adapter server started: " + command) : ("adapter launch failed: " + error), ok);
	return build_process_status_json();
}

core::String MetaAgentHost::stop_adapter_server()
{
	core::String error;
	const bool ok = process_manager_.stop("adapter_server", error);
	append_app_log("process", "out", ok ? "adapter server stopped" : ("stop failed: " + error), ok);
	return build_process_status_json();
}

core::String MetaAgentHost::set_ue5_runtimes_enabled(const core::String& body)
{
	std::lock_guard<std::mutex> lock(mutex_);
	bool enabled = false;
	if (!net::extract_json_bool_field(body, "enabled", enabled))
	{
		return "{"
			+ net::json_bool_field("success", false) + ","
			+ net::json_string_field("message", "Missing enabled field.")
			+ "}";
	}

	ue5_runtimes_enabled_ = enabled;
	return "{"
		+ net::json_bool_field("success", true) + ","
		+ net::json_bool_field("ue5_runtimes_enabled", ue5_runtimes_enabled_)
		+ "}";
}

} // namespace metaagent::app_host
