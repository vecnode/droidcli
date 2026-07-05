#pragma once

#include "metaagent.h"
#include "media_dataset_mirror.hpp"
#include "process_manager.hpp"

#include <ctime>
#include <map>
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
	// Trained LoRA adapter inference service (LLaVA OCR -> summary), separate
	// from the Ollama text-generation endpoint above. Repo: vecnode/pre-training.
	core::String adapter_url = "http://127.0.0.1:8008";

	// Centralised build/run control over the apps metaagent coordinates.
	// media-player-cpp (openFrameworks) project dir + build/run commands.
	core::String media_player_project_dir;
	core::String media_player_build_command = "make Release";
	core::String media_player_run_command = "media-player-cpp.exe";
	// pre-training repo deploy/ dir + the uv server launch command (deploy.bat).
	core::String adapter_project_dir;
	core::String adapter_launch_command = "deploy.bat";
	// pre-training output/ dir with the corpus CSVs (*_OCR.csv, *_SUMMARIES.csv,
	// *_OBJS.csv); metaagent reads these to expose the corpus structure.
	core::String dataset_output_dir;

	// Best-effort auto-launch (not build) of the peer apps when the host starts,
	// so the user doesn't have to click RunRelease / Launch server every time.
	// No-ops silently if the matching *_project_dir is unset. Default on.
	bool auto_start_media_player = true;
	bool auto_start_adapter = true;

	// Periodic Google Programmable Search Engine ("Custom Search JSON API")
	// polling. Disabled (no-op) unless api_key + search_engine_id + query are
	// all set - get a free key + engine id at
	// https://programmablesearchengine.google.com/ (free tier: 100 queries/day).
	core::String google_api_key;
	core::String google_search_engine_id;
	core::String google_search_query;
	int32_t google_search_interval_seconds = 10;
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
	core::String dispatch_command(const core::String& command_name);
	core::String build_config_json() const;

	void on_notify(const core::String& message);
	core::String build_notify_log_json() const;

	core::String proxy_media_player_get(const core::String& path) const;
	core::String proxy_media_player_post(const core::String& path, const core::String& body);

	// Navigate the media player (next/previous/open) and push the true summary for
	// the now-current clip as its subtitle, looked up from the dataset CSV.
	core::String media_navigate(const core::String& media_path, const core::String& body);
	// Push the summary for whatever clip is currently displayed.
	core::String sync_media_subtitle();
	core::String build_media_control_log_json() const;
	core::String build_app_log_json() const;

	core::String build_ollama_status_json();
	core::String update_ollama_config(const core::String& body);
	core::String update_config(const core::String& body);

	core::String build_adapter_status_json();
	core::String proxy_adapter_summarize(const core::String& body);

	// Centralised process control (build/run media player, launch adapter server).
	core::String build_media_player();
	core::String run_media_player();
	core::String stop_media_player_process();
	core::String launch_adapter_server();
	core::String stop_adapter_server();
	core::String build_process_status_json();

	// Reads the configured dataset output/ dir CSVs and returns the corpus
	// structure (image, status, OCR + summary previews, object count).
	core::String build_dataset_json();

	// Runs one Google search request (blocking HTTP) and logs each result to
	// the app log (channel "search"), which the UI's Media Log terminal shows.
	// No-op if not configured. Safe to call from any thread.
	void run_google_search();

private:
	void wire_callbacks();
	void apply_command_side_effects(app::CommandId command);

	core::String build_media_player_url(const core::String& path) const;
	void ensure_summaries_loaded();
	void push_subtitle_for_clip(const core::String& clip_name);
	// Rewrites a raw summary into a terse subtitle via Ollama (drops filler like
	// "This document outlines…"). Returns the raw summary on any failure.
	core::String condense_summary(const core::String& summary);
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
	runtime::HostServiceCallbacks host_services_;
	ai::LanguageAiRuntime language_ai_;
	ai::LanguageAiTransportCallbacks language_ai_transport_;
	net::RouteTable routes_;

	core::Array<core::String> notify_log_;
	ProcessManager process_manager_;

	// image basename -> summary, loaded from the dataset *_SUMMARIES.csv.
	std::map<core::String, core::String> summary_by_basename_;
	bool summaries_loaded_ = false;
	core::String summaries_loaded_for_dir_;
	// image basename -> Ollama-condensed subtitle (cleared when model/dataset change).
	std::map<core::String, core::String> condensed_by_basename_;

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
	float google_search_elapsed_seconds_ = 0.0f;
	bool google_search_in_flight_ = false;
	mutable std::mutex mutex_;
};

} // namespace metaagent::app_host
