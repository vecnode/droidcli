#include "metaagent_host.hpp"

#include "tools/sync_http_client.hpp"

#include <cmath>
#include <ctime>
#include <iostream>
#include <sstream>

namespace metaagent::app_host {
namespace {

core::String particle_pattern_state_name(const particle::PatternState state)
{
	switch (state)
	{
	case particle::PatternState::Idle: return "Idle";
	case particle::PatternState::Preparing: return "Preparing";
	case particle::PatternState::Anticipating: return "Anticipating";
	case particle::PatternState::Forming: return "Forming";
	case particle::PatternState::Holding: return "Holding";
	case particle::PatternState::Returning: return "Returning";
	case particle::PatternState::Dissipating: return "Dissipating";
	case particle::PatternState::Releasing: return "Releasing";
	default: return "Unknown";
	}
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

		scheduler_.pattern_config = particle::PatternConfig::make_from_preset(particle::PatternPreset::Normal);
		scheduler_.settings.manual_pattern_state_advance = true;
		scheduler_.reset_pattern_runtime();

		seed_mock_particles();
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
	scheduler_callbacks_ = particle::SchedulerCallbacks {};
	scheduler_callbacks_.build_pattern_targets = [this]() { return build_pattern_targets(); };
	scheduler_callbacks_.begin_pattern_start = [this]()
	{
		scheduler_.pattern_runtime.pattern_center = core::Vec3 {0.0f, 0.0f, 120.0f};
		return true;
	};
	scheduler_callbacks_.begin_configured_return = []() { return true; };
	scheduler_callbacks_.request_dissipate_to_center = []() { return true; };
	scheduler_callbacks_.complete_pattern_run = [this]()
	{
		seed_mock_particles();
	};
	scheduler_callbacks_.enter_pattern_state = [](particle::PatternState, particle::PatternState) {};
	scheduler_callbacks_.commit_anticipation_baseline_for_forming = []() {};
	scheduler_callbacks_.on_transition_side_effects = [](particle::PatternState) {};
	scheduler_callbacks_.log_info = [](const core::String& message)
	{
		std::cout << "[scheduler] " << message << std::endl;
	};
	scheduler_callbacks_.log_warning = [](const core::String& message)
	{
		std::cerr << "[scheduler] " << message << std::endl;
	};

	scheduler_callbacks_.particle_host.read_displayed_positions =
		[this](particle::DisplayedPose& out_pose) { return read_displayed_positions(out_pose); };
	scheduler_callbacks_.particle_host.apply_world_positions =
		[this](const core::Array<core::Vec3>& positions) { apply_world_positions(positions); };
	scheduler_callbacks_.particle_host.authoritative_particle_count =
		[this]() { return authoritative_particle_count(); };

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

void MetaAgentHost::seed_mock_particles()
{
	constexpr int32_t k_columns = 10;
	constexpr int32_t k_rows = 10;
	constexpr float k_spacing = 12.0f;

	mock_world_positions_.clear();
	mock_world_positions_.reserve(static_cast<size_t>(k_columns * k_rows));

	for (int32_t row = 0; row < k_rows; ++row)
	{
		for (int32_t column = 0; column < k_columns; ++column)
		{
			const float x = (static_cast<float>(column) - static_cast<float>(k_columns - 1) * 0.5f) * k_spacing;
			const float y = (static_cast<float>(row) - static_cast<float>(k_rows - 1) * 0.5f) * k_spacing;
			mock_world_positions_.push_back(core::Vec3 {x, y, 100.0f});
		}
	}

	auto& runtime = scheduler_.pattern_runtime;
	runtime.baseline_world_positions = mock_world_positions_;
	runtime.idle_baseline_world_positions = mock_world_positions_;
	runtime.pattern_columns = k_columns;
	runtime.pattern_center = core::Vec3 {0.0f, 0.0f, 100.0f};
}

bool MetaAgentHost::build_pattern_targets()
{
	particle::ShapeContext shape_context;
	shape_context.baseline_world_positions = mock_world_positions_;

	const particle::ShapeBuildResult result = particle::ShapeBuilder::build_pattern_targets(
		scheduler_.pattern_config,
		shape_context);
	if (!result.success)
	{
		return false;
	}

	auto& runtime = scheduler_.pattern_runtime;
	runtime.pattern_world_targets = result.pattern_world_targets;
	runtime.canonical_pattern_world_targets = result.pattern_world_targets;
	runtime.active_shape = result.resolved_shape;
	runtime.active_shape_frame = result.shape_frame;
	runtime.shape_debug_info = result.debug_info;
	runtime.pattern_columns = result.pattern_columns;
	runtime.awaiting_async_mask = false;
	return true;
}

bool MetaAgentHost::read_displayed_positions(particle::DisplayedPose& out_pose)
{
	if (mock_world_positions_.empty())
	{
		return false;
	}
	out_pose.world_positions = mock_world_positions_;
	out_pose.pattern_center = scheduler_.pattern_runtime.pattern_center;
	return true;
}

void MetaAgentHost::apply_world_positions(const core::Array<core::Vec3>& positions)
{
	if (!positions.empty())
	{
		mock_world_positions_ = positions;
	}
}

int32_t MetaAgentHost::authoritative_particle_count() const
{
	return static_cast<int32_t>(mock_world_positions_.size());
}

void MetaAgentHost::tick(float delta_seconds)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (ue5_runtimes_enabled_)
	{
		scheduler_.tick_pattern_runtime(delta_seconds, scheduler_callbacks_);
		scheduler_.tick_state_effects();
	}
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
	const auto& runtime = scheduler_.pattern_runtime;
	const runtime::RecordingSnapshot recording = runtime::invoke_query_recording(host_services_);
	const runtime::AiSnapshot ai = runtime::invoke_query_ai(host_services_);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_string_field("host", "metaagent_app") << ',';
	stream << net::json_string_field("map", session_.map_name) << ',';
	stream << net::json_string_field("build", session_.build_label) << ',';
	stream << net::json_bool_field("active", session_.active) << ',';
	stream << net::json_string_field("pattern_state", particle_pattern_state_name(runtime.state)) << ',';
	stream << net::json_string_field("pattern_status",
		scheduler_.build_pattern_status_text(
			authoritative_particle_count(),
			0)) << ',';
	stream << net::json_string_field("pattern_preset", runtime.active_config.get_preset_display_name()) << ',';
	stream << net::json_bool_field("recording", recording.capture_active) << ',';
	stream << net::json_bool_field("autopilot", ai.autopilot_enabled) << ',';
	stream << net::json_bool_field("cinematic", cinematic_enabled_) << ',';
	stream << net::json_bool_field("focus_particles", focus_particles_) << ',';
	stream << "\"particle_count\":" << authoritative_particle_count();
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
	stream << net::json_string_field("media_player_base_url", config_.media_player_base_url);
	stream << '}';
	return stream.str();
}

void MetaAgentHost::apply_command_side_effects(const app::CommandId command)
{
	switch (command)
	{
	case app::CommandId::PatternStepForward:
		scheduler_.dispatch_pattern_transition(particle::TransitionTrigger::Advance, scheduler_callbacks_);
		break;
	case app::CommandId::PatternStepBackward:
		scheduler_.dispatch_pattern_transition(particle::TransitionTrigger::Retreat, scheduler_callbacks_);
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
		stream << ',';
		stream << net::json_string_field("pattern_state",
			particle_pattern_state_name(scheduler_.pattern_runtime.state));
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
