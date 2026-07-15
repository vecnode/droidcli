#pragma once

#include "droidcli_core.h"
#include "net/connector.hpp"
#include "app/tasks.hpp"
#include "process_manager.hpp"
#include "filesystem_tools.hpp"
#include "app_index.hpp"
#include "memory_store.hpp"
#include "window_list.hpp"
#include "system_info.hpp"
#include "ffmpeg_tool.hpp"

#include <ctime>
#include <fstream>
#include <mutex>

namespace droidcli::cli {

struct HostConfig {
	bool enable_ai = true;
	core::String ollama_url = "http://127.0.0.1:11434";
	core::String ollama_model = "llama3.2";
	core::String system_prompt =
		"You are droidcli, an agent daemon with direct control of this Windows machine. "
		"You are not a chat-only assistant describing what a user could do - you have tools "
		"that actually open applications, run shell commands, read/write files, and manage "
		"background connectors and tasks, and you should call them instead of explaining how "
		"the user could do it themselves. In particular: at startup this host already scanned "
		"every application installed on this machine (Add/Remove Programs, App Paths, PATH) "
		"into an index, so you already know how to find and launch any installed app - never "
		"say you 'can't open applications' or ask the user to give you a path. If the user asks "
		"you to open an app, call open_application with the app name directly; if you aren't "
		"sure of the exact name first, call find_application to resolve it, then open it - do "
		"not stop to ask permission for an action the user already requested, only ask when a "
		"search returns multiple ambiguous matches. Use list_open_windows to check what's "
		"already running, run_command for one-shot shell work, run_ffmpeg for media "
		"transcode/convert/clip/extract/thumbnail work (it resolves the ffmpeg binary for "
		"you - always pass -y since there's no interactive prompt to answer), and the "
		"filesystem tools (read_file/write_file/list_dir/stat_path/get_cwd/which) for "
		"anything touching disk. Some tools (run_command, run_ffmpeg, write_file, "
		"open_application, launch_connector, stop_connector, enqueue_task) are side-effecting, "
		"so the host itself pauses and shows the user the exact call before running it - this "
		"happens automatically, you don't need to ask permission yourself first. If a tool "
		"result says the user declined, don't just retry the same call - explain what you were "
		"trying to do and ask what they'd prefer instead. "
		"Prefer acting over narrating: when a tool call can answer the question, call it, then "
		"report the concrete result (e.g. a PID, a file's contents, a match list) rather than a "
		"vague description of what you attempted.";
};

// DroidHost is droidcli's runtime (the Core-tier role ZeroClaw's
// zeroclaw-runtime plays - see ARCHITECTURE.md's crate comparison): it owns
// the session state, the provider seam (ai::ollama_client today, pluggable
// per the provider-abstraction extension plan), a ConnectorRegistry
// (generic http_peer / launched_process peer config replacing the old
// hardcoded adapter/media-player knowledge), a TaskQueue (persistent
// pending/running/done/failed task pipeline), and centralised process
// control for launched_process connectors.
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
	core::String build_config_json() const;
	core::String update_config(const core::String& body);

	void on_notify(const core::String& message);
	core::String build_notify_log_json() const;
	core::String build_app_log_json() const;

	core::String build_ollama_status_json();
	core::String update_ollama_config(const core::String& body);

	// Ollama install/start/model-pull setup flow (used by the TUI's in-chat
	// setup state machine when Ollama isn't ready for agent_turn()'s
	// tool-calling loop). All are blocking - call only from a context that
	// already tolerates a blocking call (HTTP route handler thread, or the
	// TUI's chat-submit handler on the FTXUI event thread).
	//
	// {"installed":bool,"online":bool,"models":[...],"configured_model":"...",
	//  "configured_model_pulled":bool}
	core::String ollama_setup_status_json();
	// Runs `winget install --id Ollama.Ollama ...`. {"ok":bool,"exit_code":...,
	// "stdout":"...","stderr":"...","error":"..."}
	core::String install_ollama();
	// Launches `ollama serve` via ProcessManager (key "__ollama__") and polls
	// /api/tags for a few seconds. {"ok":bool,"pid":...,"online":bool,"error":"..."}
	core::String start_ollama();
	// body is {"model":"..."}. Runs `ollama pull <model>` and, on success, makes
	// it the active model (same effect as update_ollama_config). {"ok":bool,
	// "model":"...","exit_code":...,"error":"..."}
	core::String pull_ollama_model(const core::String& body);

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

	// One-shot shell command execution (POST /api/run). body is
	// {"command":"...","work_dir":"...","timeout_ms":...}.
	core::String run_command(const core::String& body);

	// The ffmpeg actuator (POST /api/ffmpeg/run) - resolves the ffmpeg binary
	// (see ffmpeg_tool.hpp) and runs it with `args` as a single raw
	// argument string. body: {"args":"...","work_dir":"...","timeout_ms":...}
	// (timeout_ms defaults to 120000, longer than run_command's, since
	// encodes routinely run past 30s).
	core::String run_ffmpeg_json(const core::String& body);

	// Detached, fire-and-forget application launch (POST /api/open) - for
	// GUI apps that don't exit on their own, distinct from run_command which
	// blocks for completion. body: {"path_or_name":"...","args":"...","work_dir":"..."}.
	// Resolves via the App Paths registry, PATH, and (if those fail) the
	// installed-apps index scanned at startup - see find_applications_json.
	core::String open_application(const core::String& body);

	// Searches the installed-apps index (POST /api/apps/find) - scanned once
	// at startup from Windows' Add/Remove Programs registry entries, so it
	// covers apps that never registered on PATH or in App Paths at all.
	// body: {"query":"..."}. Returns {"matches":[{"name":...,"path":...}]}.
	core::String find_applications_json(const core::String& body) const;

	// Deterministic, LLM-free scan for "open X" style phrasing (POST
	// /api/apps/quick_open) - body: {"message":"..."}. Uses
	// intent::parse_open_intent() (pure string scanning, no Ollama call) to
	// recognize an app-launch request, then resolves it against the same
	// installed-apps index find_applications_json uses. Returns
	// {"matched":bool,"app_name":"...","ambiguous":bool,"resolved_name":"...",
	// "resolved_path":"...","candidates":[{"name":...,"path":...}]} -
	// resolved_name/resolved_path are only present when exactly one
	// candidate matched. A caller (the TUI, or any other channel) uses this
	// to react to an unambiguous open request instantly and ask the user to
	// confirm, without waiting on a local model to decide to call a tool.
	core::String try_quick_open_json(const core::String& body) const;

	// Live snapshot of currently open windows and their PIDs (GET
	// /api/apps/open) - re-enumerated fresh on every call, not cached like
	// the installed-apps index, since "open right now" changes constantly.
	// No arguments. Returns {"windows":[{"title":...,"process_name":...,"pid":...}]}.
	core::String list_open_windows_json() const;

	// Filesystem-aware agent tools (POST /api/fs/*) - droidcli executes these
	// itself, no external process or MCP server involved.
	// body: {"path":"...","max_bytes":...}. Caps read size (default 65536).
	core::String read_file(const core::String& body);
	// body: {"path":"...","content":"...","append":bool}.
	core::String write_file(const core::String& body);
	// body: {"path":"..."} (empty/omitted = droidcli's own cwd). Non-recursive.
	core::String list_dir(const core::String& body);
	// body: {"path":"..."}.
	core::String stat_path(const core::String& body);
	// No body/arguments.
	core::String get_cwd_json() const;
	// body: {"name":"..."}. Resolves an executable against PATH.
	core::String which_executable_json(const core::String& body);

	// The host machine droidcli is actually running on (GET /api/system) -
	// os_name/os_version/architecture/hostname/username/cwd, queried once at
	// initialize() and cached (see system_info_). Distinct from
	// build_status_json (droidcli's own session state), this is about the
	// environment droidcli lives in.
	core::String build_system_info_json() const;

	// The fixed tool set agent_turn() can call (GET /api/agent/tools) - name,
	// description, and JSON Schema parameters for every tool in
	// agent_tool_definitions(), so a caller (the TUI's Tools panel, or any
	// other channel) can list what the agent is capable of without
	// duplicating that list. No arguments. Returns {"tools":[{"name":...,
	// "description":...,"parameters_json_schema":...}]}.
	core::String build_agent_tools_json() const;

	// The tool-calling agent turn (POST /api/agent/turn). body is
	// {"message":"...","clear":bool,"session_id":"..."}. Drives a bounded
	// Ollama tool-calling loop against the fixed tool set declared in
	// agent_tool_definitions(), executing each requested tool against this
	// DroidHost's own methods. Every message added to the transcript is
	// also persisted to memory_store_ under the active session id (see
	// "Persistent memory" in ARCHITECTURE.md's extension plan) - omitting
	// session_id continues whatever session this process most recently
	// used; passing a previously-returned session_id resumes that session's
	// history, including across a restart. clear:true always starts a
	// brand new session id, ignoring any session_id in the same request.
	// The response's "session_id" field is what a caller should hold onto
	// to resume this conversation later.
	//
	// Side-effecting tools (see tool_call_requires_approval in host.cpp -
	// run_command, run_ffmpeg, write_file, open_application, launch/
	// stop_connector, enqueue_task) are gated: the moment the model requests
	// one, the loop pauses instead of executing it and the response carries
	// "pending_tool_call":{"tool":...,"arguments_json":...} instead of (or
	// alongside an empty) "assistant" text - no side effect has happened
	// yet. A caller resolves it via agent_tool_decision() before the
	// conversation can continue. Read-only tools (list_dir, get_cwd,
	// get_system_info, which, etc.) still auto-run without a pause.
	core::String agent_turn(const core::String& body);

	// Resolves the tool call agent_turn() is currently paused on (POST
	// /api/agent/tool_decision). body: {"approved":bool,"session_id":"...",
	// "reason":"..."} - session_id is optional (defaults to whichever call is
	// pending) but recommended so a stale/mismatched approval from an old
	// screen doesn't land on a newer pending call; reason is an optional
	// free-text explanation fed back to the model when approved is false.
	// Executes the tool (if approved) or records a "user declined" tool
	// result (if not), then resumes the same bounded loop agent_turn() runs -
	// the response has the same shape agent_turn() would return: either a
	// normal completion, or another "pending_tool_call" if the model asks
	// for a second gated tool before it's done. Returns {"ok":false,
	// "error":"no tool call is awaiting a decision"} if nothing is pending.
	core::String agent_tool_decision(const core::String& body);

	// Loads one session's persisted message history (GET
	// /api/agent/history?session_id=...) - session_id defaults to the
	// current in-process session when omitted. Returns
	// {"session_id":"...","messages":[{"hop_index":N,"role":"...",
	// "content":"...","created_at":"..."}]}.
	core::String build_agent_history_json(const core::String& session_id) const;

	// Every session id with at least one persisted message, most recently
	// active first (GET /api/agent/sessions). Returns
	// {"session_ids":[...],"current_session_id":"..."}.
	core::String build_agent_sessions_json() const;

	// Public logging hook for host-owned background threads spawned via
	// core::spawn() (see ARCHITECTURE.md's "Spawn attribution") to report
	// their lifecycle - "spawned"/"joined"/"threw: <what>" - under the
	// "thread" channel of the same durable JSONL log everything else uses.
	// core::spawn itself has no host dependency; a caller (e.g. cli/tui.cpp)
	// wires its ThreadEventSink to this method.
	void log_thread_event(const core::String& thread_name, const core::String& event);

private:
	// session_id is optional (only "chat" channel entries currently have
	// one) - attaches to the durable JSONL log line (logs/log.jsonl) so a
	// log line can be correlated with a MemoryStore session, but is not
	// part of the in-memory app_log_/GET /api/app/log shape, which is
	// unaffected by this parameter.
	void append_app_log(
		const core::String& channel,
		const core::String& direction,
		const core::String& summary,
		bool success,
		const core::String& session_id = {});
	static core::String make_log_timestamp();
	// Full date+time (not just HH:MM:SS like make_log_timestamp) - the
	// console/in-memory log only ever covers one running session, but
	// logs/log.txt accumulates across restarts, so entries need a date too.
	static core::String make_full_log_timestamp();
	static bool should_emit_periodic_log(std::time_t now_utc, std::time_t& last_emit_utc, int32_t min_interval_seconds);

	core::Array<ai::ToolDefinition> agent_tool_definitions() const;
	// Executes one tool call by name against this host's own methods. Returns
	// the JSON result text to feed back to the model as a "tool" message.
	core::String execute_agent_tool(const core::String& tool_name, const core::String& arguments_json);

	// One already-resolved tool call this turn, recorded for the final
	// "actions" array in agent_turn()/agent_tool_decision()'s response -
	// mirrors what used to be built directly into a JSON string in-line,
	// but needs to be a real value type now since it has to survive a
	// pause/resume round-trip through pending_tool_call_.
	struct PendingToolActionRecord {
		core::String tool;
		core::String arguments_json;
		core::String result_json;
	};

	// The single tool call agent_turn()'s loop is currently paused on,
	// awaiting a decision via agent_tool_decision() - see run_agent_tool_loop.
	// Single-slot (not a per-session map) because only one agent_turn/
	// agent_tool_decision call is ever in flight at a time in this process,
	// same assumption current_session_id_ already makes.
	struct PendingAgentToolCall {
		bool active = false;
		core::String session_id;
		int hop = 0;
		// The full tool_calls list the model returned for `hop`; call_index
		// is the offset of the call awaiting a decision - everything before
		// it in this list already executed and is in `actions`.
		core::Array<ai::ToolCall> tool_calls;
		size_t call_index = 0;
		core::Array<PendingToolActionRecord> actions;
	};

	// Shared tail of agent_turn() and agent_tool_decision(): drives the
	// bounded (kMaxHops) tool-calling loop against `provider`/`tools`,
	// starting at `hop`. resume_calls/resume_call_index/actions let a caller
	// resume mid-hop after a decision was just made - pass an empty
	// resume_calls (the default for a fresh agent_turn()) to start hop 0
	// clean. Pauses and populates pending_tool_call_ instead of executing
	// the moment it hits a call tool_call_requires_approval() gates.
	core::String run_agent_tool_loop(
		const core::String& session_id,
		const core::Array<ai::ToolDefinition>& tools,
		const ai::ModelProvider& provider,
		int hop,
		core::Array<ai::ToolCall> resume_calls,
		size_t resume_call_index,
		core::Array<PendingToolActionRecord> actions);

	// Builds the same-shaped response agent_turn()/agent_tool_decision()
	// return once they hit a gated call: "ok":true (nothing has failed - the
	// turn is just paused) plus "pending_tool_call" naming the call awaiting
	// a decision, and any "actions" already executed earlier in this turn.
	static core::String build_pending_tool_call_response(
		const core::String& session_id,
		const ai::ToolCall& call,
		const core::Array<PendingToolActionRecord>& actions_so_far);

	// A short, sortable-enough id ("2026-07-15T12-30-00-4f2a") - readable in
	// logs/history listings without needing a UUID library dependency.
	static core::String generate_session_id();
	// Appends one message to both agent_transcript_ and memory_store_ under
	// the given session - the single call site every transcript mutation in
	// agent_turn goes through, so the two never drift out of sync.
	void record_agent_message(const core::String& session_id, ai::ChatRole role, const core::String& content);

	HostConfig config_;
	session::RuntimeSession session_;
	ai::LanguageAiRuntime language_ai_;
	ai::LanguageAiTransportCallbacks language_ai_transport_;
	net::RouteTable routes_;

	core::Array<core::String> notify_log_;
	ProcessManager process_manager_;
	net::ConnectorRegistry connectors_;
	app::TaskQueue tasks_;
	core::Array<InstalledApp> installed_apps_;

	// The host machine's OS/hostname/architecture, queried once in
	// initialize() (see system_info.hpp) - not meant to change during a run.
	SystemInfo system_info_;

	// Persistent agent-turn history (see "Persistent memory" in
	// ARCHITECTURE.md's extension plan) - memory_store_ is the durable
	// SQLite-backed log; current_session_id_ is which session agent_turn is
	// actively appending to. Generated fresh at initialize() (no auto-resume
	// across a restart - a caller opts into resuming a prior session by
	// passing its id explicitly in an agent_turn request).
	MemoryStore memory_store_;
	core::String current_session_id_;

	struct AppLogEntry {
		core::String timestamp;
		core::String channel;
		core::String direction;
		core::String summary;
		bool success = false;
	};

	core::Array<AppLogEntry> app_log_;
	// Durable session log (logs/log.txt) - every append_app_log() call also
	// writes here, flushed immediately, so a crash doesn't lose history the
	// in-memory app_log_ (capped, gone on restart) can't provide. Opened once
	// in initialize(); a closed/never-opened stream is silently skipped.
	std::ofstream log_file_;

	// Dedicated multi-turn transcript for the agent tool-calling loop, kept
	// separate from language_ai_ (LanguageAiRuntime is single-shot request/
	// response and doesn't model a tool_calls -> tool-result -> follow-up
	// hop cycle).
	core::Array<ai::ChatMessage> agent_transcript_;

	// Set only while a gated tool call is awaiting the user's decision - see
	// PendingAgentToolCall and run_agent_tool_loop.
	PendingAgentToolCall pending_tool_call_;

	mutable std::mutex mutex_;
};

} // namespace droidcli::cli
