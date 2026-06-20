#pragma once

#include "metaagent.h"
#include "media_dataset_mirror.hpp"

#include <ctime>
#include <mutex>

namespace metaagent::app_host {

struct HostConfig {
	bool enable_ai = true;
	core::String ollama_url = "http://127.0.0.1:11434";
	core::String ollama_model = "llama3.2";
	core::String system_prompt =
		"You are a concise assistant embedded in the MetaAgent desktop application.";
	core::String media_player_base_url = "http://127.0.0.1:8080";
	core::String media_data_directory;
};

class MetaAgentHost {
public:
	void configure(const HostConfig& config);
	void initialize();

	void tick(float delta_seconds);

	session::RuntimeSession& session();
	const session::RuntimeSession& session() const;

	net::HandlerContext make_handler_context();
	net::RouteDispatchResult dispatch_route(const net::HttpRequest& request);

	core::String build_status_json() const;
	core::String build_network_status_json() const;
	core::String build_runtime_catalog_json() const;
	core::String build_gui_catalog_json() const;
	core::String dispatch_command(const core::String& command_name);
	core::String build_config_json() const;

	void on_notify(const core::String& message);
	core::String build_notify_log_json() const;

	core::String proxy_media_player_get(const core::String& path) const;
	core::String proxy_media_player_post(const core::String& path, const core::String& body);
	core::String build_media_control_log_json() const;
	core::String build_app_log_json() const;

	core::String build_ollama_status_json();
	core::String update_ollama_config(const core::String& body);
	core::String set_ue5_runtimes_enabled(const core::String& body);

private:
	void wire_callbacks();
	void seed_mock_particles();
	bool build_pattern_targets();
	bool read_displayed_positions(particle::DisplayedPose& out_pose);
	void apply_world_positions(const core::Array<core::Vec3>& positions);
	int32_t authoritative_particle_count() const;
	void apply_command_side_effects(app::CommandId command);

	core::String build_media_player_url(const core::String& path) const;
	void append_media_control_log(
		const core::String& action,
		const core::String& summary,
		bool success,
		const core::String& direction = "out");
	void append_app_log(const core::String& channel, const core::String& direction, const core::String& summary, bool success);
	static core::String make_log_timestamp();
	static bool should_emit_periodic_log(std::time_t now_utc, std::time_t& last_emit_utc, int32_t min_interval_seconds);

	HostConfig config_;
	session::RuntimeSession session_;
	particle::ParticleScheduler scheduler_;
	particle::SchedulerCallbacks scheduler_callbacks_;
	runtime::HostServiceCallbacks host_services_;
	ai::LanguageAiRuntime language_ai_;
	ai::LanguageAiTransportCallbacks language_ai_transport_;
	net::RouteTable routes_;

	core::Array<core::Vec3> mock_world_positions_;
	core::Array<core::String> notify_log_;

	struct MediaControlLogEntry {
		core::String timestamp;
		core::String channel;
		core::String direction;
		core::String action;
		core::String summary;
		bool success = false;
	};

	core::Array<MediaControlLogEntry> media_control_log_;
	MediaDatasetMirror media_dataset_mirror_;
	std::time_t last_status_out_log_utc_ = 0;
	std::time_t last_status_fail_log_utc_ = 0;
	std::time_t last_clips_fallback_log_utc_ = 0;
	bool media_player_online_last_seen_ = true;
	bool recording_active_ = false;
	bool autopilot_enabled_ = false;
	bool cinematic_enabled_ = false;
	bool focus_particles_ = false;
	bool ue5_runtimes_enabled_ = false;
	mutable std::mutex mutex_;
};

} // namespace metaagent::app_host
