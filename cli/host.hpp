#pragma once

#include "droidcli.h"
#include "net/connector.hpp"
#include "app/tasks.hpp"
#include "process_manager.hpp"

#include <ctime>
#include <mutex>

namespace droidcli::cli {

struct HostConfig {
	bool enable_ai = true;
	core::String ollama_url = "http://127.0.0.1:11434";
	core::String ollama_model = "llama3.2";
	core::String system_prompt =
		"You are a concise assistant embedded in the droidcli agent daemon.";
};

// DroidHost is the headless agent daemon core: it owns the session state, the
// Ollama text-gen seam, a ConnectorRegistry (generic http_peer / launched_process
// peer config replacing the old hardcoded adapter/media-player knowledge), a
// TaskQueue (persistent pending/running/done/failed task pipeline), and
// centralised process control for launched_process connectors.
class DroidHost {
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
	core::String update_config(const core::String& body);

	void on_notify(const core::String& message);
	core::String build_notify_log_json() const;
	core::String build_app_log_json() const;

	core::String build_ollama_status_json();
	core::String update_ollama_config(const core::String& body);

	// Connector registry (generic peer config: http_peer or launched_process).
	core::String register_connector(const core::String& body);
	bool unregister_connector(const core::String& connector_id);
	core::String list_connectors_json() const;
	core::String connector_status_json(const core::String& connector_id);
	core::String launch_connector(const core::String& connector_id);
	core::String stop_connector(const core::String& connector_id);
	core::String call_connector(
		const core::String& connector_id,
		const core::String& path,
		const core::String& method,
		const core::String& body);

	// Task queue.
	core::String enqueue_task(const core::String& body);
	core::String list_tasks_json() const;
	core::String task_status_json(const core::String& task_id) const;
	// Claims and dispatches one pending task (call_connector or launch_connector
	// depending on the task command), marking it complete/fail. Call periodically
	// from the daemon loop.
	void tick_tasks();

	core::String build_process_status_json();

private:
	void wire_callbacks();
	void apply_command_side_effects(app::CommandId command);

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
	net::ConnectorRegistry connectors_;
	app::TaskQueue tasks_;

	struct AppLogEntry {
		core::String timestamp;
		core::String channel;
		core::String direction;
		core::String summary;
		bool success = false;
	};

	core::Array<AppLogEntry> app_log_;
	bool recording_active_ = false;
	bool autopilot_enabled_ = false;
	mutable std::mutex mutex_;
};

} // namespace droidcli::cli
