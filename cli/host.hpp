#pragma once

#include "droidcli_core.h"
#include "classify/turn_decision.hpp"
#include "net/connector.hpp"
#include "app/tasks.hpp"
#include "process_manager.hpp"
#include "filesystem_tools.hpp"
#include "clipboard.hpp"
#include "app_index.hpp"
#include "memory_store.hpp"
#include "window_list.hpp"
#include "system_info.hpp"
#include "hardware_info.hpp"
#include "ffmpeg_tool.hpp"

#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>

namespace droidcli::cli {

struct HostConfig {
	bool enable_ai = true;
	// Off by default - a human opts in once, at process start (--enable-hardware-scan),
	// rather than the daemon deciding on its own to enumerate CPU/GPU/disk
	// facts about the machine it's running on. See "droidcli-hardware" in
	// ARCHITECTURE.md's crate-by-crate mapping / "Hardware awareness" section
	// for why this is scoped to read-only local inventory, not device control.
	bool enable_hardware_scan = false;
	core::String ollama_url = "http://127.0.0.1:11434";
	core::String ollama_model = "llama3.2";
	// Requested on every Ollama chat call (ai::OllamaConfig::num_ctx) so a
	// long agent-turn transcript can't be silently truncated by Ollama's own
	// (often smaller) per-model default context window - matches
	// OpenClaude's own 32768-token default. A knob, not a fixed constant -
	// see --ollama-num-ctx in cli/droidcli.cpp and "Context window
	// (num_ctx)" in ARCHITECTURE.md's OpenClaude comparison.
	int32_t ollama_num_ctx = 32768;
	// Second ai::ModelProvider (see "Second ModelProvider" in
	// ARCHITECTURE.md) - "ollama" (default) or "anthropic". Selects which
	// provider DroidHost::make_model_provider() constructs; the
	// ollama_url/ollama_model fields above are simply ignored when this is
	// "anthropic", same as anthropic_api_key/anthropic_model are ignored
	// when this is "ollama".
	core::String ai_provider = "ollama";
	core::String anthropic_api_key;
	core::String anthropic_model = "claude-3-5-haiku-latest";
	// Config hardening (see "Config hardening" in ARCHITECTURE.md). Empty by
	// default (no persistence - e.g. for a test harness that never calls
	// configure() with a real path); cli/droidcli.cpp's main() sets this to
	// its own resolved --settings path. When non-empty, DroidHost re-saves
	// the model/provider fields it owns to this file every time they change
	// at runtime (update_config/update_ollama_config), not just at process
	// startup - see "Model/provider changes persist at runtime too" in
	// ARCHITECTURE.md.
	core::String settings_path;
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
		"search returns multiple ambiguous matches. If the user asks what Windows panels/settings/"
		"locations you can open (or describes one loosely, e.g. 'the thing that shows memory "
		"usage'), call list_windows_locations and answer from that real list - never guess or "
		"invent a name. Use list_open_windows to check what's "
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
		"vague description of what you attempted. Never say 'I'll run/execute/create that' or "
		"'hold on, processing' without actually calling the tool in that exact same response - "
		"a sentence describing an action is not the action, and the user is left with nothing "
		"having happened. If you have enough information to act, call the tool now; if you don't, "
		"ask the specific question you need answered instead of claiming you're already acting. "
		"Every tool result has an \"ok\" field - that is the ONLY thing that tells you whether the "
		"action actually succeeded, never assume success from a tool simply having run, from the "
		"presence of output, or from a plausible-looking exit code deep in a JSON blob. If \"ok\" is "
		"false, the action did NOT happen: read \"failure_reason\"/\"error\" and tell the user "
		"honestly what went wrong, then either fix the call and retry or ask them how to proceed - "
		"never report success anyway, and never invent a file path, output location, or result you "
		"did not get back from a tool. If you have not called a tool for something, you have not "
		"done it, no matter how confident that sounds - use get_cwd/list_dir/stat_path to check "
		"where something actually landed rather than guessing or making up a plausible-sounding path. "
		"You have a persistent memory of run_command/run_ffmpeg bugs already solved before, across "
		"this and every past session - call search_command_fixes with a short query BEFORE "
		"attempting a command similar to a kind of task that has failed before (a specific ffmpeg "
		"filter, a specific error message), so you don't waste attempts rediscovering the same fix. "
		"After you fix something that failed at least once - a verified \"ok\":true result, not a "
		"guess - call record_command_fix with the exact broken and working command/args and one "
		"short, reusable lesson, so the next time (in this session or a future one) doesn't start "
		"from scratch. Don't record a command that worked on the first try - only ones you had to "
		"fix. Call self_status before claiming you can't do something, or right after a tool call "
		"fails and you want to know if it's an isolated problem or something wider (e.g. the AI "
		"backend itself is down). A degraded Ollama connection does not mean you're broken - the "
		"host keeps running and every non-AI tool (run_command, filesystem, connectors, tasks) "
		"still works normally; report the degradation honestly instead of going silent or refusing "
		"unrelated work. get_hardware_info reports this machine's CPU/GPU/RAM/disk inventory, but "
		"only if the human enabled it at startup - if it comes back with \"enabled\":false, tell "
		"the user hardware scanning is off and how to turn it on (--enable-hardware-scan), don't "
		"claim the data doesn't exist or make up plausible-sounding specs. If run_command or "
		"run_ffmpeg returns \"ok\":false, do not stop and ask the user whether you should retry - "
		"read \"failure_reason\", fix the command yourself based on the actual error, and call the "
		"tool again immediately in this same response. You get multiple automatic attempts at this "
		"before you have to report failure, so use them; only after you've genuinely tried a "
		"corrected command and it still fails should you explain the real error to the user. You "
		"always have the ability to run commands directly - never tell the user to run a command "
		"themselves or claim command execution is beyond your capabilities, that is never true here. "
		"Before calling copy_file/move_path/delete_file on a file the user referred to by description "
		"rather than its exact path (\"the green image on the Desktop\", \"that file\"), call list_dir "
		"on the real directory first to find its actual name - never invent a plausible-looking path "
		"or a template like \"/path/to/file.png\", both are rejected outright and waste an attempt. "
		"When you write, copy, move, or run a command that creates a file with no location specified "
		"at all (a bare filename with no directory in it, and no work_dir passed to run_command/"
		"run_ffmpeg), it lands on the user's real Desktop by default, not droidcli's own working "
		"directory - this happens automatically, you don't need to construct the Desktop path "
		"yourself. Report the actual location from the result's \"resolved_path\"/\"resolved_work_dir\" "
		"field when present, not from what you originally typed. There is exactly ONE real Desktop "
		"folder on this machine - desktop_path, from get_system_info/your system prompt - never "
		"construct, guess, or invent any other path containing a \"desktop\" segment (e.g. "
		"\"C:\\desktop\\...\", a root-level guess); if a path you're about to use has \"desktop\" in it "
		"anywhere and isn't exactly the real desktop_path, that's wrong - use the real path literally "
		"instead of typing the word \"desktop\" yourself.";
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

	// The model name actually in use right now - config_.ollama_model or
	// config_.anthropic_model, whichever config_.ai_provider selects (same
	// logic already used for classify_via_llm's per-classification-call
	// telemetry logging). Reflects whatever the settings file resolved to
	// at startup (Phase 33) or has been changed to at runtime since (Phase
	// 36) - "last session's model" and "the current model" are the same
	// value by construction, not two things to reconcile. Empty only if
	// config_ was somehow never configured at all (HostConfig's own default
	// is a real model name, not empty) - the TUI shows "none" for that case.
	core::String active_model_name() const;

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

	// Task queue. body: {"connector_id":"...","command":"launch|stop|run|<http path>",
	// "payload_json":"...","delay_ms":...,"recurrence_ms":...}. delay_ms
	// (optional) defers when the task becomes claimable by tick_tasks() -
	// e.g. delay_ms:120000 runs it no sooner than two minutes from now.
	// recurrence_ms (optional, "Phase 28") makes the task recurring - after
	// each run it's automatically rescheduled recurrence_ms later instead of
	// terminating, cron/SOP-style, without a separate scheduler subsystem
	// (see Task::recurrence_ms/scheduled_for_ms, src/app/tasks.hpp). Returns
	// {"ok":bool,"id":"...","scheduled_for_ms":...} (0 if not scheduled).
	core::String enqueue_task(const core::String& body);
	core::String list_tasks_json() const;
	core::String task_status_json(const core::String& task_id) const;
	// Stops a task for good - the only way to end a recurring task
	// (Task::recurrence_ms > 0), which would otherwise keep rescheduling
	// itself forever. No-op (ok:false) for an unknown task id or one already
	// in a terminal, non-recurring state. Gated (tool_call_requires_approval) -
	// same "recording/managing state, but still a real side effect" treatment
	// as enqueue_task. Returns {"ok":bool,"error":"..."}.
	core::String cancel_task_json(const core::String& task_id);
	// Claims and dispatches one pending task (call_connector or launch_connector
	// depending on the task command), marking it complete/fail. Call periodically
	// from the daemon loop.
	void tick_tasks();

	core::String build_process_status_json();

	// Aggregated self-health snapshot (GET /api/agent/self_status, and the
	// self_status agent tool) - answers "am I actually capable of acting
	// right now" from state already tracked elsewhere, without a fresh
	// network round-trip on every call: cached Ollama reachability (updated
	// by the watchdog check folded into tick(), not pinged inline here),
	// connector/task counts, memory-store health, and a recent-failure count
	// from app_log_. See "Phase 9" in ARCHITECTURE.md.
	core::String build_self_status_json() const;

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
	// body: {"source_path":"...","destination_path":"..."}. Files only, not
	// directories; overwrites an existing destination file. Gated (see
	// tool_call_requires_approval) - see "Phase 15" in ARCHITECTURE.md.
	core::String copy_file_json(const core::String& body);
	// body: {"source_path":"...","destination_path":"..."}. Files or
	// directories (std::filesystem::rename); overwrites an existing
	// destination file. Gated.
	core::String move_path_json(const core::String& body);
	// body: {"path":"..."}. Files only, not directories - no recursive delete.
	// Gated.
	core::String delete_file_json(const core::String& body);
	// body: {"path":"..."}. Real directory (std::filesystem::create_directories,
	// so missing parents are created too), not a file - "create a folder" was
	// previously faked via write_file with empty content, which creates a
	// file, not a directory. Idempotent (ok:true if the directory already
	// exists). Gated.
	core::String create_directory_json(const core::String& body);

	// OS clipboard access (Phase 15, ARCHITECTURE.md) - the same
	// implementation cli/tui.cpp's 'y' (copy chat transcript) keybinding
	// uses (cli/clipboard.hpp). No arguments; read is not gated (read-only).
	core::String read_clipboard_json() const;
	// body: {"text":"..."}. Replaces the clipboard's current content. Gated.
	core::String write_clipboard_json(const core::String& body);

	// The host machine droidcli is actually running on (GET /api/system) -
	// os_name/os_version/architecture/hostname/username/cwd, queried once at
	// initialize() and cached (see system_info_). Distinct from
	// build_status_json (droidcli's own session state), this is about the
	// environment droidcli lives in.
	core::String build_system_info_json() const;

	// Read-only local hardware inventory (GET /api/hardware, and the
	// get_hardware_info agent tool) - CPU name/core count, total RAM, GPU
	// adapter name(s), and per-volume disk capacity. Only populated if
	// HostConfig::enable_hardware_scan was on at startup (the human's
	// one-time opt-in - see HostConfig above); otherwise returns
	// {"ok":true,"enabled":false,...} honestly rather than fabricating
	// empty-looking data. Scanned once at initialize(), same cache-once
	// pattern as build_system_info_json/get_system_info.
	core::String build_hardware_info_json() const;

	// The fixed tool set agent_turn() can call (GET /api/agent/tools) - name,
	// description, and JSON Schema parameters for every tool in
	// agent_tool_definitions(), so a caller (the TUI's Tools panel, or any
	// other channel) can list what the agent is capable of without
	// duplicating that list. No arguments. Returns {"tools":[{"name":...,
	// "description":...,"parameters_json_schema":...}]}.
	core::String build_agent_tools_json() const;

	// Persists a "this broke, this fixed it" lesson (see CommandLesson,
	// cli/memory_store.hpp) - POST /api/agent/lessons, and the
	// record_command_fix agent tool. body: {"tool":"run_command"|
	// "run_ffmpeg"|...,"broken":"...","failure_reason":"...","working":
	// "...","lesson":"short takeaway"}. The model decides when to call this
	// - there's no automatic capture from a failed-then-succeeded pair in
	// the same turn, since the next attempt happening to work doesn't
	// reliably mean that specific change is what fixed it. Read/write-
	// adjacent (recording knowledge, not touching the OS) so it's not
	// gated behind tool_call_requires_approval() the way run_command/
	// run_ffmpeg themselves are. Returns {"ok":bool}.
	core::String record_command_fix_json(const core::String& body);

	// Looks up previously recorded lessons (POST /api/agent/lessons/search,
	// and the search_command_fixes agent tool) - case-insensitive substring
	// match against tool/broken/failure_reason/lesson, most recent first.
	// body: {"query":"..."}. Returns {"ok":true,"lessons":[{"tool":...,
	// "broken":...,"failure_reason":...,"working":...,"lesson":...,
	// "created_at":...}]}.
	core::String search_command_fixes_json(const core::String& body) const;

	// Remembers a name -> real path mapping (see KnownLocation, cli/
	// memory_store.hpp) - the remember_location agent tool. body:
	// {"name":"...","path":"..."}. path is resolved (bare "desktop/..."
	// token, if any) and verified against the real filesystem via stat_path
	// before it's stored - a location that doesn't actually exist right now
	// is never remembered. A second call with the same name (case-
	// insensitive) updates the stored path rather than duplicating it. Same
	// "recording knowledge, not touching the OS" rationale as
	// record_command_fix_json above - not gated behind
	// tool_call_requires_approval(). Returns {"ok":bool,"resolved_path":...}.
	core::String remember_location_json(const core::String& body);

	// Lists every remembered KnownLocation, most recently updated first,
	// alongside "where we are right now" (cwd, desktop_path) and other real
	// OS locations (system_locations - Home, Documents, Downloads, Program
	// Files, from SystemInfo/cli/system_info.cpp) - the get_known_locations
	// agent tool and the TUI's Locations panel both read this. No arguments.
	// Returns {"ok":true,"cwd":...,"desktop_path":...,
	// "system_locations":[{"name":...,"path":...}],
	// "known_locations":[{"name":...,"resolved_path":...,"updated_at":...}]}.
	core::String list_known_locations_json() const;

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

	// Public logging hook for the TUI's deterministic quick-open flow
	// (try_quick_open_json recognition, the yes/no confirmation the human
	// answers, and the resulting launch decision - cli/tui.cpp) so that path
	// is visible in logs/log.jsonl and the App Log panel like every other
	// action, not just the bare open_application() call it ends in. Before
	// this, the only trace of a quick-open launch was the "open" channel
	// entry open_application() itself logs - no record of what was
	// recognized, what was asked, or what the human answered, which made a
	// real incident (a quick-open launch appearing with no visible cause)
	// hard to diagnose from logs/log.jsonl alone.
	void log_quick_open_event(const core::String& summary, bool success = true);

	// Public logging hook for TUI-local chat-pane entries that never round-trip
	// through agent_turn()/agent_tool_decision() - approval-prompt replies,
	// "Approved."/"Declined."/"Cancelled." banners, clipboard-copy feedback,
	// new-session/resume banners, and caught-exception messages. Those calls
	// already log themselves (every "chat" channel entry from classify_turn/
	// execute_decision_or_pause/finish_turn_after_execution goes through
	// append_app_log); this covers everything the TUI itself
	// prints into the chat pane without going through DroidHost first, so the
	// durable log (logs/log.jsonl, git-ignored - see logs/README.md) and
	// db/droidcli_memory.sqlite3's history are a complete record of what
	// actually appeared on screen, not just the model-driven half of it.
	// role is the same ChatEntry role string the TUI already uses
	// ("user"/"info"/"error"/etc) - logged as-is under the "chat" channel,
	// direction=role, success=(role != "error").
	void log_chat_entry(const core::String& role, const core::String& text);

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
	//
	// extra_json_fields is optional, raw `"key":value` JSON field text (no
	// leading/trailing comma, no wrapping braces - append_app_log adds the
	// comma) appended into the same JSONL object. Lets a specific call site
	// (e.g. the "ollama" channel's per-hop telemetry - see "Ollama
	// telemetry" in ARCHITECTURE.md) carry structured fields beyond the
	// fixed summary/success shape without widening that shape for every
	// other channel. Never reaches the console line or the in-memory
	// app_log_/GET /api/app/log entry, both of which stay summary-only.
	void append_app_log(
		const core::String& channel,
		const core::String& direction,
		const core::String& summary,
		bool success,
		const core::String& session_id = {},
		const core::String& extra_json_fields = {});
	static core::String make_log_timestamp();
	// Full date+time (not just HH:MM:SS like make_log_timestamp) - the
	// console/in-memory log only ever covers one running session, but
	// logs/log.txt accumulates across restarts, so entries need a date too.
	static core::String make_full_log_timestamp();
	static bool should_emit_periodic_log(std::time_t now_utc, std::time_t& last_emit_utc, int32_t min_interval_seconds);

	// Throttled Ollama-reachability check, folded into tick() rather than run
	// on its own thread: a fresh /api/tags GET only every kWatchdogIntervalSeconds,
	// caching the result into ollama_reachable_/ollama_last_check_ms_/
	// ollama_last_check_error_ so build_self_status_json() and the model (via
	// the self_status tool) can notice a degraded Ollama without agent_turn
	// having to discover it mid-conversation, and without blocking the poll
	// loop on a network call every ~200ms tick. Only logs on a state
	// transition (reachable <-> unreachable), not every check, to avoid
	// spamming app_log_ while Ollama is simply off (--no-ai / not installed
	// yet). See "Phase 9" in ARCHITECTURE.md.
	void tick_watchdog();

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

	// The single tool call agent_turn()'s classify->execute->phrase pipeline
	// is currently paused on, awaiting a decision via agent_tool_decision().
	// Single-slot (not a per-session map) because only one agent_turn/
	// agent_tool_decision call is ever in flight at a time in this process,
	// same assumption current_session_id_ already makes. One decision per
	// turn now (see classify::TurnDecision), so this is a single ToolCall,
	// not an array + an index into it the way it was when a hop could
	// contain several calls.
	struct PendingAgentToolCall {
		bool active = false;
		core::String session_id;
		ai::ToolCall decided_call;
		core::Array<PendingToolActionRecord> actions;
	};

	// Constructs the active ai::ModelProvider from config_.ai_provider -
	// "ollama" (default) or "anthropic" - so agent_turn()/
	// agent_tool_decision() each need one call instead of duplicating the
	// OllamaConfig-building block that used to live at both call sites. A
	// unique_ptr (not a stack value bound to a reference, the pre-Phase-32
	// pattern) because the concrete type is now a runtime choice, not fixed
	// at compile time. See "Second ModelProvider" in ARCHITECTURE.md.
	std::unique_ptr<ai::ModelProvider> make_model_provider() const;

	// Re-saves the model/provider fields config_ owns (ollama_url,
	// ollama_model, ai_provider, anthropic_model, anthropic_api_key,
	// enable_ai, enable_hardware_scan) to config_.settings_path, so a
	// runtime change (POST /api/config, POST /api/ollama/config, or the
	// TUI's model picker - all of which call update_config()/
	// update_ollama_config()) survives a restart the same way a --ollama-model
	// CLI flag already does (Phase 33). A no-op if settings_path is empty.
	//
	// Loads the existing file first and only overlays the fields above -
	// `port` and `api_token` are left exactly as already on disk, since
	// DroidHost has no authoritative copy of either (see HostConfig; the
	// bearer token deliberately never reaches DroidHost at all - only
	// cli/droidcli.cpp and the HTTP server touch it). This must never
	// silently blank out an already-persisted encrypted token.
	//
	// Must be called with mutex_ already held (matches this file's other
	// _locked-by-caller helpers) - it only touches config_ and does file
	// I/O, no further locking of its own.
	void persist_current_settings_locked() const;

	// "Classify -> Execute -> Phrase" (see ARCHITECTURE.md's "The agent
	// turn"): agent_turn()'s actual driver. classify_turn() decides at most
	// one action for this turn - a
	// deterministic recognizer match, or exactly one LLM classification call
	// (classify_via_llm) if neither recognizer fires. execute_decision_or_pause()
	// runs that decision through the unchanged tool_call_requires_approval/
	// precheck_and_resolve_gated_call gate. finish_turn_after_execution()
	// is the shared tail of agent_turn() and agent_tool_decision() once a
	// decision has actually executed: one bounded auto-retry (via
	// classify_via_llm, never by re-running the deterministic recognizers)
	// if the result failed and the tool is retriable, then phrase_result().
	classify::TurnDecision classify_turn(
		const core::String& session_id,
		const core::String& user_message,
		const core::String& previous_assistant_text,
		const core::Array<ai::ToolDefinition>& tools,
		const ai::ModelProvider& provider);

	// The one-LLM-call classification step itself - deterministic recognizers
	// are not tried here (classify_turn already did, or a retry deliberately
	// skips them - see finish_turn_after_execution). Takes the response's
	// first tool_call only if it returned any (discarding the rest, and the
	// accompanying assistant_message - never shown to the user, this is what
	// removes the fabrication surface), else a PlainReply from
	// assistant_message. Never persists its own assistant_message into
	// agent_transcript_ - the caller records exactly one Assistant message
	// per turn, whatever text actually ends up shown.
	classify::TurnDecision classify_via_llm(
		const core::String& session_id,
		const core::Array<ai::ToolDefinition>& tools,
		const ai::ModelProvider& provider);

	// Runs one decided {tool_name, arguments_json} through the exact same
	// gate every LLM-decided call already goes through. Returns true and
	// sets out_tool_result if execution completed (appending to `actions`);
	// returns false and sets out_pending_response if it paused for human
	// approval instead (pending_tool_call_ already set) - the caller must
	// return out_pending_response immediately without touching `actions`
	// further. `already_approved` (true only for a pending_command bare-"yes"
	// match) skips the gate entirely, exactly like today's behavior.
	bool execute_decision_or_pause(
		const core::String& session_id,
		const core::String& tool_name,
		const core::String& arguments_json,
		core::Array<PendingToolActionRecord>& actions,
		bool already_approved,
		core::String& out_tool_result,
		core::String& out_pending_response);

	// Shared tail of agent_turn() and agent_tool_decision() once a decision
	// has executed: one bounded auto-retry on a retriable tool's failure,
	// then phrase_result() and the final response JSON.
	// `allow_retry=false` for a user-declined gated call (agent_tool_decision
	// with approved=false): a decline is an explicit human veto, not a
	// technical failure the model has anything to correct - auto-retrying
	// that would mean asking the model to "fix and try again" something the
	// user just said no to.
	core::String finish_turn_after_execution(
		const core::String& session_id,
		const core::String& executed_tool_name,
		const core::String& executed_arguments_json,
		const core::String& executed_result_json,
		core::Array<PendingToolActionRecord> actions,
		const core::Array<ai::ToolDefinition>& tools,
		const ai::ModelProvider& provider,
		bool allow_retry = true);

	// The "Phrase" step: classify::try_template_reply first (zero LLM calls,
	// zero fabrication risk for the common cases), else phrase_via_llm.
	core::String phrase_result(
		const core::String& tool_name,
		const core::String& arguments_json,
		const core::String& result_json,
		const ai::ModelProvider& provider);

	// A second, distinct ai::ModelProvider call with no tools attached (so
	// parse_response's tool_calls is guaranteed empty) - given only the
	// ground-truth result_json and a fixed instruction to phrase it without
	// claiming anything beyond it. Falls back to generic_result_sentence() on
	// any transport/HTTP failure rather than ever dropping the reply.
	core::String phrase_via_llm(
		const ai::ModelProvider& provider,
		const core::String& tool_name,
		const core::String& result_json) const;

	// The generic, zero-LLM-call fallback sentence phrase_via_llm() itself
	// falls back to if its own call fails - built only from result_json's
	// "ok"/"error" fields, never a guess.
	core::String generic_result_sentence(const core::String& result_json) const;

	// Builds the same {"ok":true,"assistant":...,"session_id":...,
	// "actions":[...]} shape agent_turn()/agent_tool_decision() have always
	// returned on a completed (non-paused) turn - no "budget_exhausted"
	// field anymore, since there's no hop budget left to exhaust.
	core::String build_final_agent_response(
		const core::String& session_id,
		const core::String& assistant_text,
		const core::Array<PendingToolActionRecord>& actions) const;

	// Builds the same-shaped response agent_turn()/agent_tool_decision()
	// return once they hit a gated call: "ok":true (nothing has failed - the
	// turn is just paused) plus "pending_tool_call" naming the call awaiting
	// a decision, and any "actions" already executed earlier in this turn.
	// Not static (unlike before) - now calls display_arguments_with_full_paths,
	// which needs system_info_.
	core::String build_pending_tool_call_response(
		const core::String& session_id,
		const ai::ToolCall& call,
		const core::Array<PendingToolActionRecord>& actions_so_far) const;

	// The result of running the Windows execution ruleset's trust-ordered
	// resolution (see "Windows execution ruleset" in ARCHITECTURE.md)
	// against a bare name or path - shared by open_application() itself and
	// precheck_and_resolve_gated_call()'s pre-approval check below, so the
	// two can never drift into resolving the same input differently.
	struct ResolvedLaunchTarget {
		bool resolved = false;
		core::String target;         // full path to hand to launch_application
		core::String effective_args; // args, filled in from a matched well-known target if the caller gave none
		core::String source;         // "given_path" | "app_paths_registry" | "installed_apps_index" | "windows_known_location" | "path_search"
		core::String error_message;  // set only when resolved is false
	};
	ResolvedLaunchTarget resolve_open_application_target(const core::String& path_or_name, const core::String& args) const;

	// Runs resolve_open_application_target BEFORE a gated open_application
	// call is ever shown to the user for yes/no approval, and rewrites
	// `call`'s arguments_json in place to the fully-resolved target if one
	// was found - so what the human approves and what actually executes are
	// guaranteed identical and already-verified to exist, never a proposal
	// that's certain to fail. If nothing resolves, returns false with
	// out_result_json already holding the same-shaped failure JSON
	// open_application() itself would return - the caller records that as a
	// completed (failed) action and continues the loop instead of ever
	// proposing a yes/no for something that cannot succeed. For
	// `run_command`/`run_ffmpeg`, also rewrites an invented/placeholder/empty
	// `work_dir` to the real Desktop in place (see resolve_work_dir_or_desktop)
	// before the human ever sees the approval prompt - a model-invented
	// `work_dir` must never even be shown as something to approve. A no-op
	// (returns true, arguments_json untouched) for any other tool. See "Never
	// propose an unresolvable action" in ARCHITECTURE.md.
	bool precheck_and_resolve_gated_call(ai::ToolCall& call, core::String& out_result_json) const;

	// The single source of truth for "what work_dir should a shell command
	// actually run in": an empty, placeholder-looking (`looks_like_placeholder_path`),
	// or invented-desktop (`looks_like_invented_desktop_path`) value all
	// resolve to the real, OS-resolved Desktop path - never a made-up one the
	// model typed. A real, non-placeholder value still gets
	// `substitute_bare_desktop_token` applied first (a bare "desktop/..."
	// token still needs the real prefix). Used by both
	// precheck_and_resolve_gated_call (pre-approval) and
	// run_command()/run_ffmpeg_json() (execution) - the same defense-in-depth
	// shape the Windows execution ruleset already uses for `open_application`.
	core::String resolve_work_dir_or_desktop(const core::String& requested_work_dir) const;

	// Returns `arguments_json` with any well-known path field ("path", or
	// "source_path"/"destination_path" for copy/move) rewritten to a full
	// absolute path, for display in an approval prompt only - never used for
	// the arguments a tool actually executes with, so this can't weaken any
	// existing path-fabrication guard (looks_like_placeholder_path,
	// looks_like_invented_desktop_path in src/reliability/path_guards.hpp),
	// which still run unchanged, at execution time, against the original
	// unmodified arguments_json. A path that already looks like a
	// placeholder/invented one is deliberately left as-is rather than
	// absolute-ified, so an obviously-fake path still reads as obviously
	// fake to the human approving it, not laundered into something that
	// merely looks more legitimate. run_command/run_ffmpeg (free-form
	// shell/ffmpeg argument strings, not a single JSON path field) and every
	// other tool are returned unchanged - see "Full paths in the approval
	// prompt" in ARCHITECTURE.md.
	core::String display_arguments_with_full_paths(const core::String& tool_name, const core::String& arguments_json) const;

	// A short, sortable-enough id ("2026-07-15T12-30-00-4f2a") - readable in
	// logs/history listings without needing a UUID library dependency.
	static core::String generate_session_id();
	// Appends one message to both agent_transcript_ and memory_store_ under
	// the given session - the single call site every transcript mutation in
	// agent_turn goes through, so the two never drift out of sync.
	void record_agent_message(const core::String& session_id, ai::ChatRole role, const core::String& content);

	HostConfig config_;
	session::RuntimeSession session_;
	ai::LanguageRuntime language_ai_;
	ai::LanguageTransportCallbacks language_ai_transport_;
	net::RouteTable routes_;

	core::Array<core::String> notify_log_;
	ProcessManager process_manager_;
	net::ConnectorRegistry connectors_;
	app::TaskQueue tasks_;
	core::Array<InstalledApp> installed_apps_;

	// The host machine's OS/hostname/architecture, queried once in
	// initialize() (see system_info.hpp) - not meant to change during a run.
	SystemInfo system_info_;

	// CPU/GPU/RAM/disk inventory, queried once in initialize() only if
	// config_.enable_hardware_scan was on (see HardwareInfo, hardware_info.hpp).
	// Left default-constructed (all zero/empty) when scanning is disabled -
	// build_hardware_info_json() reports "enabled":false rather than treating
	// zeroed-out fields as real data.
	HardwareInfo hardware_info_;

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
	// separate from language_ai_ (LanguageRuntime is single-shot request/
	// response and doesn't model a tool_calls -> tool-result -> follow-up
	// hop cycle).
	core::Array<ai::ChatMessage> agent_transcript_;

	// Set only while a gated tool call is awaiting the user's decision - see
	// PendingAgentToolCall and execute_decision_or_pause.
	PendingAgentToolCall pending_tool_call_;

	// Watchdog state (see tick_watchdog()) - optimistic defaults so a host
	// started with --no-ai or before the first check ever runs doesn't
	// falsely report Ollama as down.
	bool ollama_reachable_ = true;
	int64_t ollama_last_check_ms_ = 0;
	core::String ollama_last_check_error_;
	std::time_t watchdog_last_check_utc_ = 0;
	static constexpr int32_t kWatchdogIntervalSeconds = 15;

	mutable std::mutex mutex_;
};

} // namespace droidcli::cli
