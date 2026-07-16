#include "tui.hpp"

#include "net/json.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace droidcli::cli {
namespace {

// Extracts each top-level object of a named JSON array, e.g. {"tasks":[{...}]}.
// Hand-rolled brace-depth walk, mirroring extract_connector_objects() in
// droidcli.cpp - consistent with the rest of net/json.hpp's no-JSON-library
// convention, generalized here to any array key so it covers connectors,
// tasks, and app-log entries with one function.
std::vector<std::string> extract_json_object_array(const std::string& json, const std::string& key)
{
	std::vector<std::string> objects;
	const std::string needle = "\"" + key + "\":";
	const size_t array_key = json.find(needle);
	if (array_key == std::string::npos)
	{
		return objects;
	}
	size_t cursor = json.find('[', array_key);
	if (cursor == std::string::npos)
	{
		return objects;
	}
	++cursor;

	while (cursor < json.size())
	{
		while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'
			|| json[cursor] == '\n' || json[cursor] == '\r' || json[cursor] == ','))
		{
			++cursor;
		}
		if (cursor >= json.size() || json[cursor] == ']')
		{
			break;
		}
		if (json[cursor] != '{')
		{
			break;
		}

		size_t depth = 0;
		const size_t start = cursor;
		for (; cursor < json.size(); ++cursor)
		{
			if (json[cursor] == '{')
			{
				++depth;
			}
			else if (json[cursor] == '}')
			{
				--depth;
				if (depth == 0)
				{
					++cursor;
					break;
				}
			}
		}
		objects.push_back(json.substr(start, cursor - start));
	}

	return objects;
}

struct ConnectorRow {
	std::string id;
	std::string kind;
	std::string base_url;
	std::string launch_cmd;
	bool enabled = true;
	std::string live_status = "unknown";
};

struct TaskRow {
	std::string id;
	std::string connector_id;
	std::string command;
	std::string status;
	// Absolute epoch-ms deadline (0 = runnable immediately) - see
	// Task::scheduled_for_ms, src/app/tasks.hpp. Rendered as a countdown in
	// the Tasks panel while still in the future.
	int64_t scheduled_for_ms = 0;
};

std::vector<ConnectorRow> parse_connectors(const std::string& json)
{
	std::vector<ConnectorRow> rows;
	for (const std::string& object : extract_json_object_array(json, "connectors"))
	{
		ConnectorRow row;
		row.id = net::extract_json_string_field(object, "id");
		row.kind = net::extract_json_string_field(object, "kind");
		row.base_url = net::extract_json_string_field(object, "base_url");
		row.launch_cmd = net::extract_json_string_field(object, "launch_cmd");
		bool enabled = true;
		if (net::extract_json_bool_field(object, "enabled", enabled))
		{
			row.enabled = enabled;
		}
		if (!row.id.empty())
		{
			rows.push_back(row);
		}
	}
	return rows;
}

std::vector<TaskRow> parse_tasks(const std::string& json)
{
	std::vector<TaskRow> rows;
	for (const std::string& object : extract_json_object_array(json, "tasks"))
	{
		TaskRow row;
		row.id = net::extract_json_string_field(object, "id");
		row.connector_id = net::extract_json_string_field(object, "connector_id");
		row.command = net::extract_json_string_field(object, "command");
		row.status = net::extract_json_string_field(object, "status");
		int64_t scheduled_for_ms = 0;
		net::extract_json_int_field(object, "scheduled_for_ms", scheduled_for_ms);
		row.scheduled_for_ms = scheduled_for_ms;
		if (!row.id.empty())
		{
			rows.push_back(row);
		}
	}
	return rows;
}

struct ToolRow {
	std::string name;
	std::string description;
};

// The agent's fixed tool set (GET /api/agent/tools) - fetched once at
// startup, not polled like tasks/connectors, since it never changes while
// droidcli is running (agent_tool_definitions() is a fixed list built at
// call time from DroidHost's own methods, not runtime state).
std::vector<ToolRow> parse_tools(const std::string& json)
{
	std::vector<ToolRow> rows;
	for (const std::string& object : extract_json_object_array(json, "tools"))
	{
		ToolRow row;
		row.name = net::extract_json_string_field(object, "name");
		row.description = net::extract_json_string_field(object, "description");
		if (!row.name.empty())
		{
			rows.push_back(row);
		}
	}
	return rows;
}

// One entry of GET /api/app/log, kept structured (not flattened to a string)
// so the log panel can color by channel/success and pick out real tool
// executions ("tool <name>(...) -> ...", see append_app_log calls in
// cli/host.cpp) instead of narrated chat text - see "Phase 9 follow-up:
// log coloring + execution visibility" in ARCHITECTURE.md.
struct LogRow {
	std::string timestamp;
	std::string channel;
	std::string direction;
	std::string summary;
	bool success = true;
};

std::vector<LogRow> parse_log_lines(const std::string& json)
{
	std::vector<LogRow> rows;
	for (const std::string& object : extract_json_object_array(json, "entries"))
	{
		LogRow row;
		row.timestamp = net::extract_json_string_field(object, "timestamp");
		row.channel = net::extract_json_string_field(object, "channel");
		row.direction = net::extract_json_string_field(object, "direction");
		row.summary = net::extract_json_string_field(object, "summary");
		bool success = true;
		if (net::extract_json_bool_field(object, "success", success))
		{
			row.success = success;
		}
		rows.push_back(row);
	}
	return rows;
}

// Channels that mean droidcli actually launched or ran something on the
// host machine (a real process, not just chat narration) - "run" (run_command),
// "ffmpeg" (run_ffmpeg), "open" (open_application), "process"
// (launch_connector/stop_connector via ProcessManager). Used to give process
// launches their own unmistakable color in the log panel.
bool is_process_launch_channel(const std::string& channel)
{
	return channel == "run" || channel == "ffmpeg" || channel == "open" || channel == "process"
		|| channel == "quick_open";
}

// Boils DroidHost::connector_status_json()'s two response shapes (launched_process
// vs http_peer, see cli/host.cpp) down to one short display string.
std::string summarize_status(const std::string& kind, const std::string& status_json)
{
	bool ok = false;
	net::extract_json_bool_field(status_json, "ok", ok);
	if (!ok)
	{
		return "error";
	}
	if (kind == "launched_process")
	{
		bool running = false;
		net::extract_json_bool_field(status_json, "running", running);
		const std::string status_text = net::extract_json_string_field(status_json, "status");
		return running ? ("running: " + status_text) : ("stopped: " + status_text);
	}
	bool online = false;
	net::extract_json_bool_field(status_json, "online", online);
	return online ? "online" : "offline";
}

// State computed on the background poller thread, handed off to the FTXUI
// event-loop thread via a mutex + Event::Custom nudge. Only the poller thread
// writes here; only the event loop reads/clears it (see run_tui below).
struct PolledState {
	std::mutex mutex;
	std::vector<ConnectorRow> connectors;
	std::vector<TaskRow> tasks;
	std::vector<LogRow> log_lines;
};

// One line of the chat panel: who said it (drives color/weight) and the text.
struct ChatEntry {
	std::string role; // "user" | "assistant" | "tool" | "error" | "info"
	std::string text;
};

// Plain-text rendering of the chat transcript (one line per entry, with the
// same [USER]/[AGENT]/[SYSTEM] prefixes the chat panel shows, minus color)
// for copying to the clipboard.
std::string format_chat_transcript(const std::vector<ChatEntry>& entries)
{
	std::ostringstream stream;
	for (const ChatEntry& entry : entries)
	{
		if (entry.role == "user")
		{
			stream << "[USER] " << entry.text << "\n";
		}
		else if (entry.role == "assistant")
		{
			stream << "[AGENT] " << entry.text << "\n";
		}
		else if (entry.role == "tool")
		{
			stream << "  " << entry.text << "\n";
		}
		else if (entry.role == "error")
		{
			stream << "error: " << entry.text << "\n";
		}
		else
		{
			stream << "[SYSTEM] " << entry.text << "\n";
		}
	}
	return stream.str();
}

// Chat results computed on a detached background thread (agent turns, Ollama
// install/start/pull can all take seconds to minutes) and handed off to the
// FTXUI event-loop thread the same way PolledState is: the background thread
// appends here under the mutex and posts Event::Custom; only the event loop
// drains it (see run_tui below). This is what keeps the chat input and the
// rest of the UI responsive instead of freezing for the duration of a
// network call - previously the whole handler ran inline on the FTXUI event
// thread, so the screen could not redraw (the just-cleared input looked
// "stuck" showing stale text) until the call finished.
struct ChatWork {
	std::mutex mutex;
	std::vector<ChatEntry> pending_entries;
	bool clear_in_flight = false;
	// Set whenever an agent_turn response carries a "session_id" - drained
	// into the UI thread's current_session_id the same way pending_entries
	// is, so the TUI can display it and include it in the next request
	// (resuming a session across a restart - see "Persistent memory" in
	// ARCHITECTURE.md). Empty means "no update this round", not "clear it".
	std::string session_id;
	// Set whenever a response carries a "pending_tool_call" - drained into
	// the UI thread's pending_tool_approval the same way session_id is, so
	// the TUI shows the proposal and waits for a yes/no instead of treating
	// the turn as finished. See PendingToolApproval below.
	bool has_pending_tool_call = false;
	std::string pending_tool_name;
	std::string pending_tool_args;
};

// Extracts DroidHost::agent_turn()/agent_tool_decision()'s response
// ({"ok":bool,"assistant":"...","session_id":"...","actions":[{"tool":"...",
// "arguments_json":"...","result_json":"..."}],"pending_tool_call":
// {"tool":"...","arguments_json":"..."},"error":"..."}) into chat lines: one
// line per tool call already executed this turn, plus either the assistant's
// final reply or (if the loop paused instead of finishing) a proposal line
// asking the user to approve/decline the pending call - never both, since a
// paused response carries no "assistant" text yet. out_session_id is set
// whenever the response carries one (including on some error paths - see
// agent_turn()'s doc comment in cli/host.hpp) so the caller can persist it
// for resuming this conversation later. out_has_pending_tool_call/
// out_pending_tool_name/out_pending_tool_args are set only when the loop
// paused - the caller (run_chat_turn/run_tool_decision below) stages them
// into ChatWork so the UI thread can start a PendingToolApproval.
std::vector<ChatEntry> parse_agent_turn_response(
	const std::string& json,
	std::string& out_session_id,
	bool& out_has_pending_tool_call,
	std::string& out_pending_tool_name,
	std::string& out_pending_tool_args)
{
	std::vector<ChatEntry> entries;
	out_session_id = net::extract_json_string_field(json, "session_id");
	out_has_pending_tool_call = false;
	out_pending_tool_name.clear();
	out_pending_tool_args.clear();

	bool ok = false;
	net::extract_json_bool_field(json, "ok", ok);
	if (!ok)
	{
		const std::string error = net::extract_json_string_field(json, "error");
		entries.push_back(ChatEntry{"error", error.empty() ? "agent turn failed" : error});
		return entries;
	}

	for (const std::string& action : extract_json_object_array(json, "actions"))
	{
		const std::string tool = net::extract_json_string_field(action, "tool");
		const std::string args = net::extract_json_string_field(action, "arguments_json");
		entries.push_back(ChatEntry{"tool", "called " + tool + "(" + args + ")"});
	}

	// build_pending_tool_call_response() (cli/host.cpp) always writes
	// "pending_tool_call" before "actions" in the response, so slicing from
	// its key onward and taking the first "tool"/"arguments_json" match
	// lands on the pending call's own fields, not a later action's -
	// matches this file's existing first-occurrence JSON field lookups
	// (extract_json_object_array et al.), not a real parser.
	const size_t pending_index = json.find("\"pending_tool_call\":");
	if (pending_index != std::string::npos)
	{
		const std::string pending_object = json.substr(pending_index);
		out_has_pending_tool_call = true;
		out_pending_tool_name = net::extract_json_string_field(pending_object, "tool");
		out_pending_tool_args = net::extract_json_string_field(pending_object, "arguments_json");
		entries.push_back(ChatEntry{"info",
			"[AGENT WANTS TO] " + out_pending_tool_name + "(" + out_pending_tool_args + ") - approve? (yes/no, or say why not)"});
		return entries;
	}

	const std::string assistant = net::extract_json_string_field(json, "assistant");
	entries.push_back(ChatEntry{"assistant", assistant.empty() ? "(no reply)" : assistant});
	return entries;
}

// One installed-app candidate from DroidHost::try_quick_open_json()'s
// "candidates" array.
struct AppOpenCandidate {
	std::string name;
	std::string path;
};

// Mirrors DroidHost::try_quick_open_json()'s response shape - the
// deterministic, LLM-free "open X" recognizer (intent::parse_open_intent)
// plus its resolution against the installed-apps index.
struct QuickOpenResult {
	bool matched = false;
	std::string app_name;
	std::vector<AppOpenCandidate> candidates;
};

QuickOpenResult parse_quick_open_result(const std::string& json)
{
	QuickOpenResult result;
	net::extract_json_bool_field(json, "matched", result.matched);
	if (!result.matched)
	{
		return result;
	}
	result.app_name = net::extract_json_string_field(json, "app_name");
	for (const std::string& object : extract_json_object_array(json, "candidates"))
	{
		AppOpenCandidate candidate;
		candidate.name = net::extract_json_string_field(object, "name");
		candidate.path = net::extract_json_string_field(object, "path");
		if (!candidate.name.empty())
		{
			result.candidates.push_back(candidate);
		}
	}
	return result;
}

// UI-thread-owned state for the quick-open confirmation flow: set once a
// message parses as an "open X" request, cleared once the user answers
// (yes/no, a candidate number, or "cancel"). While active, the next Enter
// press is consumed by this flow instead of being sent to Ollama.
struct PendingOpen {
	bool active = false;
	std::string app_name;
	// Empty: not found in the installed-apps index, confirm opening
	// app_name directly. One entry: an unambiguous index match, confirm
	// yes/no. More than one: ambiguous, ask the user to pick a number.
	std::vector<AppOpenCandidate> candidates;
};

std::string describe_pending_open_prompt(const PendingOpen& pending)
{
	if (pending.candidates.size() > 1)
	{
		std::ostringstream stream;
		stream << "Multiple installed apps match '" << pending.app_name << "': ";
		for (size_t index = 0; index < pending.candidates.size(); ++index)
		{
			if (index > 0)
			{
				stream << "  ";
			}
			stream << (index + 1) << ") " << pending.candidates[index].name;
		}
		stream << ". Type a number to open one, or 'cancel'.";
		return stream.str();
	}
	if (pending.candidates.size() == 1)
	{
		return "Open " + pending.candidates[0].name + " (" + pending.candidates[0].path + ")? (yes/no)";
	}
	return "'" + pending.app_name + "' isn't in the installed-apps index, but I can still try to open it "
		"directly by that name. Open it? (yes/no)";
}

// Mirrors DroidHost::agent_turn()/agent_tool_decision()'s "pending_tool_call"
// field - set whenever the agent's tool-calling loop pauses on a
// side-effecting tool (run_command, run_ffmpeg, write_file,
// open_application, launch/stop_connector, enqueue_task; see
// tool_call_requires_approval in cli/host.cpp) instead of running it. The
// TUI holds this the same way it holds PendingOpen: the next Enter is
// treated as the yes/no (or free-text decline reason) that resolves it,
// not as a new chat message.
struct PendingToolApproval {
	bool active = false;
	std::string session_id;
	std::string tool;
	std::string arguments_json;
};

// Parses a plain JSON array of strings, e.g. {"models":["a","b"]}. Distinct
// from extract_json_object_array above, which walks an array of {...}
// objects - DroidHost::ollama_setup_status_json()'s "models" field is an
// array of bare strings instead.
std::vector<std::string> parse_json_string_array(const std::string& json, const std::string& key)
{
	std::vector<std::string> values;
	const std::string needle = "\"" + key + "\":";
	const size_t key_index = json.find(needle);
	if (key_index == std::string::npos)
	{
		return values;
	}
	size_t cursor = json.find('[', key_index);
	if (cursor == std::string::npos)
	{
		return values;
	}
	++cursor;

	while (cursor < json.size())
	{
		while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'
			|| json[cursor] == '\n' || json[cursor] == '\r' || json[cursor] == ','))
		{
			++cursor;
		}
		if (cursor >= json.size() || json[cursor] == ']')
		{
			break;
		}
		if (json[cursor] != '"')
		{
			break;
		}
		++cursor;
		std::string value;
		while (cursor < json.size() && json[cursor] != '"')
		{
			if (json[cursor] == '\\' && cursor + 1 < json.size())
			{
				value += json[cursor + 1];
				cursor += 2;
				continue;
			}
			value += json[cursor++];
		}
		if (cursor < json.size())
		{
			++cursor; // skip closing quote
		}
		values.push_back(value);
	}
	return values;
}

// Mirrors DroidHost::ollama_setup_status_json()'s response shape.
struct OllamaSetupStatus {
	bool installed = false;
	bool online = false;
	std::vector<std::string> models;
	std::string configured_model;
	bool configured_model_pulled = false;
};

OllamaSetupStatus parse_ollama_setup_status(const std::string& json)
{
	OllamaSetupStatus status;
	net::extract_json_bool_field(json, "installed", status.installed);
	net::extract_json_bool_field(json, "online", status.online);
	status.models = parse_json_string_array(json, "models");
	status.configured_model = net::extract_json_string_field(json, "configured_model");
	net::extract_json_bool_field(json, "configured_model_pulled", status.configured_model_pulled);
	return status;
}

// Drives the TUI's hardcoded in-chat Ollama setup flow (install -> start ->
// pull a model), recomputed fresh from ollama_setup_status_json() on every
// submitted chat message rather than tracked as persistent state - simplest
// thing that reflects reality even if the user fixes Ollama out-of-band
// (e.g. installs it themselves in another window) between messages.
enum class OllamaSetupState { Ready, NeedsInstall, NeedsStart, NeedsModel };

OllamaSetupState compute_setup_state(const OllamaSetupStatus& status)
{
	if (!status.installed)
	{
		return OllamaSetupState::NeedsInstall;
	}
	if (!status.online)
	{
		return OllamaSetupState::NeedsStart;
	}
	if (status.models.empty() || !status.configured_model_pulled)
	{
		return OllamaSetupState::NeedsModel;
	}
	return OllamaSetupState::Ready;
}

std::string trim(const std::string& value)
{
	size_t start = 0;
	size_t end = value.size();
	while (start < end && std::isspace(static_cast<unsigned char>(value[start])))
	{
		++start;
	}
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
	{
		--end;
	}
	return value.substr(start, end - start);
}

std::string to_lower(const std::string& value)
{
	std::string result = value;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

// Copies `text` (UTF-8) to the OS clipboard. Windows-only for now, matching
// this codebase's existing Windows-first precedent (command_runner.cpp,
// process_manager.cpp) - a POSIX implementation (X11/Wayland clipboard, or
// shelling out to pbcopy on macOS) is a reasonable future addition but not
// implemented here.
bool copy_text_to_clipboard(const std::string& text)
{
#ifdef _WIN32
	if (!OpenClipboard(nullptr))
	{
		return false;
	}

	// CF_UNICODETEXT (not CF_TEXT) so non-ASCII chat content - emoji from a
	// model's reply, non-English text, etc. - survives the copy intact.
	const int wide_length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (wide_length <= 0)
	{
		CloseClipboard();
		return false;
	}

	const HGLOBAL buffer_handle = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wide_length) * sizeof(wchar_t));
	if (buffer_handle == nullptr)
	{
		CloseClipboard();
		return false;
	}

	wchar_t* buffer = static_cast<wchar_t*>(GlobalLock(buffer_handle));
	if (buffer == nullptr)
	{
		GlobalFree(buffer_handle);
		CloseClipboard();
		return false;
	}
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buffer, wide_length);
	GlobalUnlock(buffer_handle);

	EmptyClipboard();
	// The clipboard owns buffer_handle after a successful SetClipboardData -
	// do not GlobalFree it ourselves.
	const bool ok = SetClipboardData(CF_UNICODETEXT, buffer_handle) != nullptr;
	if (!ok)
	{
		GlobalFree(buffer_handle);
	}
	CloseClipboard();
	return ok;
#else
	(void)text;
	return false;
#endif
}

// One line describing what the user should type next for a given setup
// state, listing pulled models by number when there are any. Shown
// proactively as soon as the TUI starts (so the user isn't left guessing
// what to type) and again any time their input doesn't match an expected
// action for the current state.
std::string describe_setup_prompt(const OllamaSetupState state, const OllamaSetupStatus& status)
{
	switch (state)
	{
	case OllamaSetupState::NeedsInstall:
		return "Ollama isn't installed. Type 'install ollama' to install it automatically "
			"(via winget), or install it yourself from ollama.com.";
	case OllamaSetupState::NeedsStart:
		return "Ollama is installed but not running. Type 'start ollama' to launch it.";
	case OllamaSetupState::NeedsModel:
	{
		if (status.models.empty())
		{
			return "No Ollama models found on this machine. Type a model name to download it "
				"(e.g. 'llama3.2', 'qwen2.5', 'mistral-nemo') and I'll pull it for you.";
		}
		std::string list_text;
		for (size_t index = 0; index < status.models.size(); ++index)
		{
			if (index > 0)
			{
				list_text += "  ";
			}
			list_text += std::to_string(index + 1) + ") " + status.models[index];
		}
		return "Ollama has these models available: " + list_text
			+ ". Type a number or name to use one (Enter = 1), or 'pull <name>' to download a different model.";
	}
	case OllamaSetupState::Ready:
	default:
		return {};
	}
}

// Resolves a trimmed chat message against the currently pulled models for
// the NeedsModel state: blank (Enter with no text) defaults to the first
// pulled model, a 1-based index or an exact case-insensitive name match
// selects that model. Returns false (and leaves out_model untouched) if the
// message doesn't resolve to an existing model - the caller then treats it
// as a new model name to pull instead.
bool try_resolve_model_selection(
	const std::string& trimmed_message,
	const OllamaSetupStatus& status,
	std::string& out_model)
{
	if (status.models.empty())
	{
		return false;
	}

	if (trimmed_message.empty())
	{
		out_model = status.models.front();
		return true;
	}

	bool is_number = !trimmed_message.empty();
	for (const char character : trimmed_message)
	{
		if (std::isdigit(static_cast<unsigned char>(character)) == 0)
		{
			is_number = false;
			break;
		}
	}
	if (is_number)
	{
		const int index = std::atoi(trimmed_message.c_str());
		if (index >= 1 && static_cast<size_t>(index) <= status.models.size())
		{
			out_model = status.models[static_cast<size_t>(index) - 1];
			return true;
		}
		return false;
	}

	const std::string lower_message = to_lower(trimmed_message);
	for (const std::string& model : status.models)
	{
		if (to_lower(model) == lower_message)
		{
			out_model = model;
			return true;
		}
	}
	return false;
}

// Where the TUI remembers which agent_turn session to resume next launch -
// alongside droidcli_state.json/droidcli_memory.sqlite3 in db/ (see
// db/README.md), git-ignored (see .gitignore). db/ is created by
// DroidHost::initialize(), called before run_tui() (see main() in
// cli/droidcli.cpp), so it already exists by the time this is touched.
// JSON, matching every other piece of droidcli's persisted state, rather
// than a bare .txt with an implicit one-line format.
const char* kLastSessionIdFile = "db/droidcli_last_session.json";

std::string read_last_session_id()
{
	std::ifstream file(kLastSessionIdFile);
	if (!file)
	{
		return {};
	}
	std::ostringstream buffer;
	buffer << file.rdbuf();
	return trim(net::extract_json_string_field(buffer.str(), "session_id"));
}

void write_last_session_id(const std::string& session_id)
{
	std::ofstream file(kLastSessionIdFile, std::ios::trunc);
	if (file)
	{
		file << '{' << net::json_string_field("session_id", session_id) << '}' << std::endl;
	}
}

// Turns DroidHost::build_agent_history_json()'s response
// ({"session_id":"...","messages":[{"hop_index":N,"role":"...",
// "content":"...","created_at":"..."}]}) into chat lines for replaying a
// resumed session into the chat panel at startup. The system-prompt message
// (always hop_index 0 when present) is summarized rather than shown in
// full - it's long by design (see HostConfig::system_prompt) and was never
// something the user typed or read the first time either.
std::vector<ChatEntry> parse_agent_history_for_resume(const std::string& json)
{
	std::vector<ChatEntry> entries;
	bool saw_system_prompt = false;
	for (const std::string& message : extract_json_object_array(json, "messages"))
	{
		const std::string role = net::extract_json_string_field(message, "role");
		const std::string content = net::extract_json_string_field(message, "content");
		if (role == "system")
		{
			saw_system_prompt = true;
			continue;
		}
		if (role == "assistant")
		{
			entries.push_back(ChatEntry{"assistant", content});
		}
		else if (role == "tool")
		{
			entries.push_back(ChatEntry{"tool", "(resumed) " + content});
		}
		else
		{
			entries.push_back(ChatEntry{"user", content});
		}
	}
	if (!entries.empty())
	{
		std::string summary = "Resumed a prior session (" + std::to_string(entries.size()) + " message"
			+ (entries.size() == 1 ? "" : "s");
		if (saw_system_prompt)
		{
			summary += ", plus the system prompt";
		}
		summary += "). Press 'n' to start a new session instead.";
		entries.insert(entries.begin(), ChatEntry{"info", summary});
	}
	return entries;
}

} // namespace

int run_tui(DroidHost& host, int http_port, volatile bool& running_flag)
{
	using namespace ftxui;

	PolledState polled;
	ScreenInteractive screen = ScreenInteractive::Fullscreen();

	// UI-thread-owned state. Only ever mutated inside the FTXUI event loop (in
	// response to Event::Custom), so it needs no locking of its own - the
	// PolledState mutex above is the only cross-thread hand-off point.
	std::vector<ConnectorRow> connectors;
	std::vector<std::string> connector_entries;
	int selected_connector = 0;
	std::vector<TaskRow> tasks;
	// Fetched once here (not part of PolledState's periodic refresh) since
	// the tool set is fixed for the process lifetime - see parse_tools().
	const std::vector<ToolRow> tools = parse_tools(host.build_agent_tools_json());
	std::vector<LogRow> log_lines;
	std::vector<ChatEntry> chat_entries;
	// Resizable-split pane widths (columns) - mutated directly by
	// ResizableSplitLeft/Right on mouse drag, see the split construction below.
	int left_pane_width = 36;
	int right_pane_width = 30;
	std::string chat_input_text;
	// Cursor position for chat_input, exposed so history recall below can
	// jump it to the end of a recalled message instead of leaving it
	// wherever it happened to be - see the InputOption passed to Input().
	int chat_input_cursor = 0;
	// Shell-style message history: every message actually sent to the agent
	// (not pending_open/pending_tool_approval yes/no replies - those aren't
	// "prompts" worth recalling), oldest first. ArrowUp/ArrowDown below walk
	// it; chat_history_cursor is -1 while editing live text, otherwise an
	// index into chat_history. chat_history_draft holds whatever was being
	// typed before the first ArrowUp, restored once ArrowDown walks back
	// past the newest entry - the same behavior a shell's history gives you.
	std::vector<std::string> chat_history;
	int chat_history_cursor = -1;
	std::string chat_history_draft;
	bool agent_turn_in_flight = false;
	PendingOpen pending_open;
	// Set whenever an agent_turn()/agent_tool_decision() response pauses on
	// a side-effecting tool call - see PendingToolApproval above.
	PendingToolApproval pending_tool_approval;
	// The active agent_turn session (see "Persistent memory" in
	// ARCHITECTURE.md) - included in every real chat request once known, so
	// a restarted TUI resumes the same conversation instead of silently
	// starting a fresh one server-side. Persisted to kLastSessionIdFile
	// whenever a response reports it; loaded back at startup below.
	std::string current_session_id;
	// Set by the 'n' keybinding: the next real chat message sends
	// "clear":true instead of the current session_id, starting a brand new
	// session (old history stays in MemoryStore, just no longer active -
	// same semantics as agent_turn()'s "clear" field).
	bool pending_new_session = false;

	// core::spawn (droidcli's zeroclaw-spawn analog - see ARCHITECTURE.md's
	// "Spawn attribution") names each background thread and reports its
	// lifecycle ("spawned"/"joined"/"threw: ...") into logs/log.jsonl under
	// the "thread" channel via DroidHost::log_thread_event, so a stuck or
	// crashed background thread is diagnosable from the log instead of just
	// eventually noticed. Declared here, before chat_input's CatchEvent
	// handler below (which spawns chat_worker), so both call sites -
	// chat_worker here and poller further down - can capture it by
	// reference.
	const droidcli::core::ThreadEventSink thread_event_sink =
		[&host](const std::string& thread_name, const std::string& event)
	{
		host.log_thread_event(thread_name, event);
	};

	// Fires the actual launch for a confirmed quick-open request and reports
	// the result - reuses DroidHost::open_application (the same path
	// find_application/open_application tool calls go through), so a quick
	// open gets identical PID reporting, App Paths/PATH/index resolution,
	// and app_log auditing as an LLM-driven open would.
	auto perform_quick_open = [&](const std::string& path_or_name, const std::string& display_name)
	{
		const std::string body = "{" + net::json_string_field("path_or_name", path_or_name) + "}";
		const std::string result_json = host.open_application(body);
		bool launched = false;
		net::extract_json_bool_field(result_json, "launched", launched);
		// log_quick_open_event puts this decision in logs/log.jsonl and the
		// App Log panel under its own "quick_open" channel - before this, a
		// quick-open launch left no trace besides open_application()'s own
		// "open"-channel line, with nothing explaining what was recognized or
		// that the human confirmed it (see "Phase 9 follow-up" in
		// ARCHITECTURE.md for the real incident this closes).
		host.log_quick_open_event("confirmed - launching " + display_name, launched);
		if (launched)
		{
			chat_entries.push_back(ChatEntry{"info", "Opened " + display_name + "."});
		}
		else
		{
			const std::string error = net::extract_json_string_field(result_json, "error");
			chat_entries.push_back(ChatEntry{"error",
				"Could not open " + display_name + (error.empty() ? "." : (": " + error))});
		}
	};
	const std::string status_line =
		"droidcli TUI  -  HTTP API on http://127.0.0.1:" + std::to_string(http_port);

	auto rebuild_connector_entries = [&]()
	{
		connector_entries.clear();
		for (const ConnectorRow& row : connectors)
		{
			std::ostringstream label;
			label << (row.enabled ? "[on]  " : "[off] ") << row.id
				<< "  (" << row.kind << ")  " << row.live_status;
			connector_entries.push_back(label.str());
		}
		if (selected_connector >= static_cast<int>(connector_entries.size()))
		{
			selected_connector = connector_entries.empty() ? 0 : static_cast<int>(connector_entries.size()) - 1;
		}
		if (selected_connector < 0)
		{
			selected_connector = 0;
		}
	};

	Component connector_menu = Menu(&connector_entries, &selected_connector);
	connector_menu = CatchEvent(connector_menu, [connector_menu](Event event) mutable -> bool
	{
		// FTXUI's Menu already handles ArrowUp/ArrowDown; add j/k as aliases.
		if (event == Event::j)
		{
			return connector_menu->OnEvent(Event::ArrowDown);
		}
		if (event == Event::k)
		{
			return connector_menu->OnEvent(Event::ArrowUp);
		}
		return false;
	});

	Component tasks_view = Renderer([&]() -> Element
	{
		// paragraph() (not text()) on each cell so a long task/connector id
		// wraps within its column instead of overflowing into the next one.
		Elements rows;
		rows.push_back(hbox({
			text("ID") | size(WIDTH, EQUAL, 20),
			text("CONNECTOR") | size(WIDTH, EQUAL, 14),
			text("COMMAND") | size(WIDTH, EQUAL, 9),
			text("WHEN") | size(WIDTH, EQUAL, 12),
			text("STATUS") | flex,
		}) | bold);
		const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		for (const TaskRow& task : tasks)
		{
			// A task's scheduled_for_ms is only meaningful while it's still
			// pending - once claimed it runs immediately regardless, so a
			// "done"/"failed"/"running" row with a past deadline shows blank
			// rather than a stale "in -12s".
			std::string when = "now";
			if (task.status == "pending" && task.scheduled_for_ms > now_ms)
			{
				const int64_t seconds_left = (task.scheduled_for_ms - now_ms) / 1000;
				when = "in " + std::to_string(seconds_left) + "s";
			}
			rows.push_back(hbox({
				paragraph(task.id) | size(WIDTH, EQUAL, 20),
				paragraph(task.connector_id) | size(WIDTH, EQUAL, 14),
				paragraph(task.command) | size(WIDTH, EQUAL, 9),
				paragraph(when) | size(WIDTH, EQUAL, 12),
				paragraph(task.status) | flex,
			}));
		}
		if (tasks.empty())
		{
			rows.push_back(text("(no tasks queued)") | dim);
		}
		return vbox(rows) | frame | flex;
	});

	Component tools_view = Renderer([&]() -> Element
	{
		Elements rows;
		for (const ToolRow& tool : tools)
		{
			rows.push_back(hbox({
				text(tool.name) | bold | size(WIDTH, EQUAL, 20),
				paragraph(tool.description) | flex,
			}));
		}
		if (tools.empty())
		{
			rows.push_back(text("(no tools reported)") | dim);
		}
		return vbox(rows) | frame | flex;
	});

	// Mounted into right_column below (see "Phase 9" in ARCHITECTURE.md).
	// Color-coded so failures and real process launches stand out from
	// ordinary chat narration at a glance:
	//  - a failed entry (success:false) is always red, regardless of channel -
	//    this is the "something actually went wrong" signal.
	//  - a real tool execution ("tool <name>(...) -> ...", logged only when
	//    a tool call actually ran - see execute_agent_tool call sites in
	//    cli/host.cpp) is bold green, distinct from the assistant's own
	//    narration text right above/below it in the same channel. This is
	//    the answer to "is it actually launching something": if you don't
	//    see a green "tool ..." line, nothing ran, no matter what the
	//    assistant claimed in chat.
	//  - a process-launch channel (run/ffmpeg/open/process -
	//    is_process_launch_channel) gets its own magenta so an actual OS-level
	//    process launch/command execution is unmistakable even scrolling fast.
	//  - watchdog/task/thread channels get their own dimmer colors so the
	//    high-volume chat channel doesn't drown them out visually.
	Component log_view = Renderer([&]() -> Element
	{
		// paragraph() (not text()) so a long log line wraps within the panel's
		// width instead of overflowing off the right edge, matching chat_log_view.
		Elements lines;
		const size_t max_visible_lines = 200;
		const size_t start = log_lines.size() > max_visible_lines ? log_lines.size() - max_visible_lines : 0;
		for (size_t index = start; index < log_lines.size(); ++index)
		{
			const LogRow& row = log_lines[index];
			const std::string line = "[" + row.timestamp + "] [" + row.channel + "] [" + row.direction + "] " + row.summary;
			Element element = paragraph(line);
			if (!row.success)
			{
				element |= color(Color::Red);
			}
			else if (row.channel == "chat" && row.summary.rfind("tool ", 0) == 0)
			{
				element |= color(Color::Green);
				element |= bold;
			}
			else if (is_process_launch_channel(row.channel))
			{
				element |= color(Color::Magenta);
				element |= bold;
			}
			else if (row.channel == "watchdog")
			{
				element |= color(Color::Yellow);
			}
			else if (row.channel == "task")
			{
				element |= color(Color::Cyan);
			}
			else if (row.channel == "thread")
			{
				element |= dim;
			}
			lines.push_back(element);
		}
		if (lines.empty())
		{
			lines.push_back(text("(no log entries yet)") | dim);
		}
		// Keep the view pinned to the newest entry, same rationale as
		// chat_log_view below - new log lines otherwise append below the
		// visible scroll area and the panel looks stalled.
		lines.back() |= focus;
		return vbox(lines) | yframe | flex;
	});

	Component chat_log_view = Renderer([&]() -> Element
	{
		Elements lines;
		for (const ChatEntry& entry : chat_entries)
		{
			// paragraph() (not text()) wraps within the panel's width instead
			// of overflowing off the right edge.
			if (entry.role == "user")
			{
				lines.push_back(paragraph("[USER] " + entry.text) | bold | color(Color::Blue));
			}
			else if (entry.role == "assistant")
			{
				lines.push_back(paragraph("[AGENT] " + entry.text) | color(Color::Green));
			}
			else if (entry.role == "tool")
			{
				lines.push_back(paragraph("  " + entry.text) | dim | color(Color::Yellow));
			}
			else if (entry.role == "error")
			{
				lines.push_back(paragraph("error: " + entry.text) | color(Color::Red));
			}
			else
			{
				// [SYSTEM] (Ollama setup prompts, etc.) shows up often - an
				// explicit dark grey keeps it visually de-emphasized without
				// relying on `dim`, whose actual look varies by terminal.
				lines.push_back(paragraph("[SYSTEM] " + entry.text) | color(Color::GrayDark));
			}
		}
		if (agent_turn_in_flight)
		{
			lines.push_back(paragraph("[AGENT] (thinking...)"));
		}
		if (lines.empty())
		{
			lines.push_back(paragraph("(no messages yet - type below and press Enter)"));
		}
		// Marking the last line as the frame's focused element makes yframe
		// auto-scroll to keep it in view on every render - without this the
		// scroll position stays wherever it last was (usually the top), so
		// new messages append below the visible area and the panel looks
		// stuck/empty even though chat_entries (never truncated, unbounded)
		// still has everything.
		lines.back() |= focus;
		return vbox(lines) | yframe | flex;
	});

	ChatWork chat_work;
	std::thread chat_worker;

	// Everything that can block (agent_turn, Ollama install/start/pull) runs
	// on a detached-from-the-caller's-perspective background thread, never
	// inline on the FTXUI event thread: previously the whole handler ran
	// here directly, so the screen could not redraw for the duration of the
	// call - the just-cleared input looked "stuck" showing stale text (an
	// install can take minutes). The worker appends results to `chat_work`
	// under its mutex and posts Event::Custom; the event loop below drains
	// it into `chat_entries` and clears `agent_turn_in_flight`, the same
	// hand-off pattern PolledState already uses for the poller thread.
	//
	// Only one chat turn can be in flight at a time (`agent_turn_in_flight`
	// gates new submissions); `chat_worker` is joined before starting a new
	// one (a no-op join, since the previous turn already finished) and
	// joined once more after screen.Loop() returns, before `screen` is
	// destroyed, so a worker can never touch a dead ScreenInteractive.
	//
	// Before routing to agent_turn() (which needs a working, model-loaded
	// Ollama to decide anything), a small hardcoded state machine checks
	// Ollama's setup status in-process and, if it isn't ready, walks the user
	// through install/start/pick-or-pull-a-model instead - this path must
	// work even when Ollama/the LLM is completely unreachable, so it cannot
	// be routed through agent_turn()'s own tool-calling loop.
	//
	// The worker thread runs entirely inside a try/catch: a network failure
	// calling Ollama (or any other exception) must always surface as an
	// "error" chat entry, never propagate out - an uncaught exception on a
	// detached thread calls std::terminate() immediately and takes the whole
	// process down, which is exactly the crash this replaces.

	// Shared tail for both a normal chat turn (run_chat_turn below) and a
	// tool-approval decision (run_tool_decision below): parses one
	// agent_turn()/agent_tool_decision() response into chat entries and
	// stages its session_id/pending-tool-call fields into chat_work, so the
	// two call sites don't duplicate that logic.
	auto ingest_agent_response = [&](const std::string& response_json, std::vector<ChatEntry>& results)
	{
		std::string response_session_id;
		bool has_pending_tool_call = false;
		std::string pending_tool_name;
		std::string pending_tool_args;
		for (const ChatEntry& entry : parse_agent_turn_response(
			response_json, response_session_id, has_pending_tool_call, pending_tool_name, pending_tool_args))
		{
			results.push_back(entry);
		}
		if (!response_session_id.empty() || has_pending_tool_call)
		{
			std::lock_guard<std::mutex> lock(chat_work.mutex);
			if (!response_session_id.empty())
			{
				chat_work.session_id = response_session_id;
			}
			if (has_pending_tool_call)
			{
				chat_work.has_pending_tool_call = true;
				chat_work.pending_tool_name = pending_tool_name;
				chat_work.pending_tool_args = pending_tool_args;
			}
		}
	};

	auto run_chat_turn = [&](std::string message, bool start_new_session)
	{
		std::vector<ChatEntry> results;
		try
		{
			const std::string trimmed_message = trim(message);
			const std::string lower_message = to_lower(trimmed_message);
			const OllamaSetupStatus setup_status = parse_ollama_setup_status(host.ollama_setup_status_json());
			const OllamaSetupState setup_state = compute_setup_state(setup_status);

			if (setup_state == OllamaSetupState::NeedsInstall)
			{
				if (lower_message == "install ollama" || lower_message == "install")
				{
					results.push_back(ChatEntry{"info", "Installing Ollama, this can take a few minutes..."});
					const std::string result_json = host.install_ollama();
					bool ok = false;
					net::extract_json_bool_field(result_json, "ok", ok);
					if (ok)
					{
						results.push_back(ChatEntry{"info",
							"Ollama installed. Type a message to continue - you'll be asked to start it next."});
					}
					else
					{
						const std::string error = net::extract_json_string_field(result_json, "error");
						const std::string stderr_text = net::extract_json_string_field(result_json, "stderr");
						results.push_back(ChatEntry{"error",
							"Ollama install failed: " + (error.empty() ? stderr_text : error)});
					}
				}
				else
				{
					results.push_back(ChatEntry{"info", describe_setup_prompt(setup_state, setup_status)});
				}
			}
			else if (setup_state == OllamaSetupState::NeedsStart)
			{
				if (lower_message == "start ollama" || lower_message == "start")
				{
					results.push_back(ChatEntry{"info", "Starting Ollama..."});
					const std::string result_json = host.start_ollama();
					bool ok = false;
					net::extract_json_bool_field(result_json, "ok", ok);
					bool online = false;
					net::extract_json_bool_field(result_json, "online", online);
					if (ok && online)
					{
						results.push_back(ChatEntry{"info", "Ollama is running. Type a message to continue."});
					}
					else if (ok)
					{
						results.push_back(ChatEntry{"error",
							"Ollama process started but is not yet reachable at its configured URL. "
							"Try again in a moment."});
					}
					else
					{
						results.push_back(ChatEntry{"error",
							"Failed to start Ollama: " + net::extract_json_string_field(result_json, "error")});
					}
				}
				else
				{
					results.push_back(ChatEntry{"info", describe_setup_prompt(setup_state, setup_status)});
				}
			}
			else if (setup_state == OllamaSetupState::NeedsModel)
			{
				if (lower_message == "list")
				{
					std::string models_text;
					for (size_t index = 0; index < setup_status.models.size(); ++index)
					{
						if (index > 0)
						{
							models_text += ", ";
						}
						models_text += setup_status.models[index];
					}
					results.push_back(ChatEntry{"info",
						"Pulled models: " + (models_text.empty() ? std::string("(none)") : models_text)});
				}
				else if (lower_message == "install" || lower_message == "install ollama"
					|| lower_message == "start" || lower_message == "start ollama")
				{
					results.push_back(ChatEntry{"info", describe_setup_prompt(setup_state, setup_status)});
				}
				else
				{
					// Blank (Enter alone) picks the first pulled model; a
					// number or an exact existing name picks that one - both
					// are just a local config write, no download needed.
					// Anything else (including an optional "pull " prefix)
					// is treated as a new model name to download.
					std::string selected_model;
					std::string pull_target;
					const bool has_pull_prefix = lower_message.rfind("pull ", 0) == 0;
					if (has_pull_prefix)
					{
						pull_target = trim(trimmed_message.substr(5));
					}

					if (!has_pull_prefix && try_resolve_model_selection(trimmed_message, setup_status, selected_model))
					{
						std::ostringstream config_body;
						config_body << '{' << net::json_string_field("model", selected_model) << '}';
						host.update_ollama_config(config_body.str());
						results.push_back(ChatEntry{"info",
							"Using model '" + selected_model + "'. Type a message to start chatting."});
					}
					else
					{
						const std::string model_to_pull = has_pull_prefix ? pull_target : trimmed_message;
						if (model_to_pull.empty())
						{
							results.push_back(ChatEntry{"info", describe_setup_prompt(setup_state, setup_status)});
						}
						else
						{
							results.push_back(ChatEntry{"info",
								"Downloading model '" + model_to_pull + "', this can take a while..."});
							std::ostringstream pull_body;
							pull_body << '{' << net::json_string_field("model", model_to_pull) << '}';
							const std::string result_json = host.pull_ollama_model(pull_body.str());
							bool ok = false;
							net::extract_json_bool_field(result_json, "ok", ok);
							if (ok)
							{
								results.push_back(ChatEntry{"info",
									"Model '" + model_to_pull + "' downloaded and set as the active model. "
									"Type a message to start chatting."});
							}
							else
							{
								results.push_back(ChatEntry{"error",
									"Failed to pull model '" + model_to_pull + "': "
									+ net::extract_json_string_field(result_json, "error")});
							}
						}
					}
				}
			}
			else
			{
				std::ostringstream request_body;
				request_body << '{' << net::json_string_field("message", message);
				if (start_new_session)
				{
					request_body << ',' << net::json_bool_field("clear", true);
				}
				else if (!current_session_id.empty())
				{
					// Resuming/continuing the session this TUI process is
					// on - see current_session_id's declaration above and
					// "Persistent memory" in ARCHITECTURE.md.
					request_body << ',' << net::json_string_field("session_id", current_session_id);
				}
				request_body << '}';
				const std::string response_json = host.agent_turn(request_body.str());
				ingest_agent_response(response_json, results);
			}
		}
		catch (const std::exception& e)
		{
			results.push_back(ChatEntry{"error", std::string("internal error: ") + e.what()});
		}
		catch (...)
		{
			results.push_back(ChatEntry{"error", "internal error: unknown exception"});
		}

		{
			std::lock_guard<std::mutex> lock(chat_work.mutex);
			for (ChatEntry& entry : results)
			{
				chat_work.pending_entries.push_back(std::move(entry));
			}
			chat_work.clear_in_flight = true;
		}
		screen.PostEvent(Event::Custom);
	};

	// Resolves a pending tool-call approval (POST /api/agent/tool_decision) -
	// the same background-thread/try-catch/drain-to-chat_work shape as
	// run_chat_turn above, since executing an approved tool (or resuming the
	// model afterward) can block just as long as a normal chat turn.
	auto run_tool_decision = [&](bool approved, std::string reason, std::string decision_session_id)
	{
		std::vector<ChatEntry> results;
		try
		{
			std::ostringstream request_body;
			request_body << '{' << net::json_bool_field("approved", approved);
			if (!reason.empty())
			{
				request_body << ',' << net::json_string_field("reason", reason);
			}
			if (!decision_session_id.empty())
			{
				request_body << ',' << net::json_string_field("session_id", decision_session_id);
			}
			request_body << '}';
			const std::string response_json = host.agent_tool_decision(request_body.str());
			ingest_agent_response(response_json, results);
		}
		catch (const std::exception& e)
		{
			results.push_back(ChatEntry{"error", std::string("internal error: ") + e.what()});
		}
		catch (...)
		{
			results.push_back(ChatEntry{"error", "internal error: unknown exception"});
		}

		{
			std::lock_guard<std::mutex> lock(chat_work.mutex);
			for (ChatEntry& entry : results)
			{
				chat_work.pending_entries.push_back(std::move(entry));
			}
			chat_work.clear_in_flight = true;
		}
		screen.PostEvent(Event::Custom);
	};

	// Records a message the user actually sent into chat_history (see its
	// declaration above) - resets the browse cursor so the next ArrowUp
	// starts from this newest entry, and skips an exact repeat of the
	// immediately preceding one so hitting Enter twice on the same text
	// doesn't clutter history with duplicates.
	auto record_chat_history = [&](const std::string& message)
	{
		if (message.empty())
		{
			return;
		}
		if (!chat_history.empty() && chat_history.back() == message)
		{
			return;
		}
		chat_history.push_back(message);
		chat_history_cursor = -1;
		chat_history_draft.clear();
	};

	InputOption chat_input_option;
	chat_input_option.cursor_position = &chat_input_cursor;
	Component chat_input = Input(&chat_input_text, "type a message, Enter to send...", chat_input_option);
	chat_input = CatchEvent(chat_input, [&](Event event) -> bool
	{
		// Intercepted here, ahead of Input's own ArrowUp/ArrowDown (which
		// only move the cursor within multi-line content - irrelevant for a
		// single-line chat box), so Up/Down always walks message history
		// regardless of where the cursor currently sits.
		if (event == Event::ArrowUp)
		{
			if (chat_history.empty())
			{
				return true;
			}
			if (chat_history_cursor == -1)
			{
				chat_history_draft = chat_input_text;
				chat_history_cursor = static_cast<int>(chat_history.size()) - 1;
			}
			else if (chat_history_cursor > 0)
			{
				--chat_history_cursor;
			}
			chat_input_text = chat_history[static_cast<size_t>(chat_history_cursor)];
			chat_input_cursor = static_cast<int>(chat_input_text.size());
			return true;
		}
		if (event == Event::ArrowDown)
		{
			if (chat_history_cursor == -1)
			{
				return true;
			}
			++chat_history_cursor;
			if (chat_history_cursor >= static_cast<int>(chat_history.size()))
			{
				chat_history_cursor = -1;
				chat_input_text = chat_history_draft;
			}
			else
			{
				chat_input_text = chat_history[static_cast<size_t>(chat_history_cursor)];
			}
			chat_input_cursor = static_cast<int>(chat_input_text.size());
			return true;
		}

		if (event == Event::Return)
		{
			if (agent_turn_in_flight)
			{
				return true;
			}

			// A gated tool call awaiting approval takes priority over
			// everything else the next Enter could mean - the agent's loop is
			// paused server-side and cannot continue until this is resolved.
			// "yes"/"y" approves; anything else declines, using the reply
			// text itself as the reason fed back to the model (empty for a
			// bare "no"/"n"/"cancel", so the model just sees a plain refusal).
			if (pending_tool_approval.active)
			{
				const std::string reply = trim(chat_input_text);
				chat_input_text.clear();
				if (!reply.empty())
				{
					chat_entries.push_back(ChatEntry{"user", reply});
				}
				const std::string lower_reply = to_lower(reply);
				const bool approved = lower_reply == "yes" || lower_reply == "y";
				const bool bare_decline = lower_reply == "no" || lower_reply == "n" || lower_reply == "cancel";
				const std::string reason = (approved || bare_decline) ? std::string() : reply;
				const std::string decision_session_id = pending_tool_approval.session_id;

				chat_entries.push_back(ChatEntry{"info",
					approved ? "Approved. Running..." : "Declined."});

				agent_turn_in_flight = true;
				pending_tool_approval = PendingToolApproval{};

				if (chat_worker.joinable())
				{
					chat_worker.join();
				}
				chat_worker = droidcli::core::spawn(
					"tui.chat_worker",
					[run_tool_decision, approved, reason, decision_session_id]()
					{ run_tool_decision(approved, reason, decision_session_id); },
					thread_event_sink);
				return true;
			}

			// Deterministic, LLM-free "open X" handling takes priority over
			// both the pending-confirmation reply and the normal Ollama/
			// agent-turn path: opening an app is common enough, and
			// consequential enough, that it shouldn't wait on (or depend on
			// the reliability of) a local model deciding to call a tool -
			// see intent::parse_open_intent (src/intent/open_intent.cpp) and
			// DroidHost::try_quick_open_json (cli/host.cpp). Every launch
			// still requires an explicit yes/no (or a number, if ambiguous)
			// from the user - this shortcuts recognition, not confirmation.
			if (pending_open.active)
			{
				const std::string reply = trim(chat_input_text);
				chat_input_text.clear();
				if (!reply.empty())
				{
					chat_entries.push_back(ChatEntry{"user", reply});
				}
				const std::string lower_reply = to_lower(reply);

				if (pending_open.candidates.size() > 1)
				{
					bool is_number = !reply.empty();
					for (const char character : reply)
					{
						if (std::isdigit(static_cast<unsigned char>(character)) == 0)
						{
							is_number = false;
							break;
						}
					}
					if (lower_reply == "cancel" || lower_reply == "no")
					{
						host.log_quick_open_event("declined - user did not confirm opening " + pending_open.app_name, false);
						chat_entries.push_back(ChatEntry{"info", "Cancelled."});
						pending_open = PendingOpen{};
					}
					else if (is_number)
					{
						const int index = std::atoi(reply.c_str());
						if (index >= 1 && static_cast<size_t>(index) <= pending_open.candidates.size())
						{
							const AppOpenCandidate& chosen = pending_open.candidates[static_cast<size_t>(index) - 1];
							perform_quick_open(chosen.path, chosen.name);
							pending_open = PendingOpen{};
						}
						else
						{
							chat_entries.push_back(ChatEntry{"info", describe_pending_open_prompt(pending_open)});
						}
					}
					else
					{
						chat_entries.push_back(ChatEntry{"info", describe_pending_open_prompt(pending_open)});
					}
				}
				else if (lower_reply == "yes" || lower_reply == "y")
				{
					if (pending_open.candidates.size() == 1)
					{
						perform_quick_open(pending_open.candidates[0].path, pending_open.candidates[0].name);
					}
					else
					{
						perform_quick_open(pending_open.app_name, pending_open.app_name);
					}
					pending_open = PendingOpen{};
				}
				else if (lower_reply == "no" || lower_reply == "n" || lower_reply == "cancel")
				{
					host.log_quick_open_event("declined - user did not confirm opening " + pending_open.app_name, false);
					chat_entries.push_back(ChatEntry{"info", "Cancelled."});
					pending_open = PendingOpen{};
				}
				else
				{
					chat_entries.push_back(ChatEntry{"info", describe_pending_open_prompt(pending_open)});
				}
				return true;
			}

			if (!chat_input_text.empty())
			{
				const QuickOpenResult quick_open = parse_quick_open_result(host.try_quick_open_json(
					"{" + net::json_string_field("message", chat_input_text) + "}"));
				if (quick_open.matched)
				{
					const std::string message = chat_input_text;
					chat_input_text.clear();
					chat_entries.push_back(ChatEntry{"user", message});
					record_chat_history(message);

					pending_open.active = true;
					pending_open.app_name = quick_open.app_name;
					pending_open.candidates = quick_open.candidates;
					host.log_quick_open_event("recognized \"" + quick_open.app_name + "\" from \"" + message + "\" - awaiting confirmation");
					chat_entries.push_back(ChatEntry{"info", describe_pending_open_prompt(pending_open)});
					return true;
				}
			}

			if (chat_input_text.empty())
			{
				// Blank Enter is only meaningful (picks the default model)
				// while NeedsModel is showing a picker. Only checked here,
				// inside the empty-input branch, so a normal chat message
				// never pays for this extra local status call on every send.
				const bool blank_allowed =
					compute_setup_state(parse_ollama_setup_status(host.ollama_setup_status_json()))
						== OllamaSetupState::NeedsModel;
				if (!blank_allowed)
				{
					return true;
				}
			}

			const std::string message = chat_input_text;
			chat_input_text.clear();
			if (!message.empty())
			{
				chat_entries.push_back(ChatEntry{"user", message});
				record_chat_history(message);
			}
			agent_turn_in_flight = true;

			// Snapshotted here (UI thread) rather than read from inside
			// run_chat_turn (background thread) to avoid a data race on
			// pending_new_session - it's reset immediately, before the
			// spawned thread can observe it, so a second 'n' press while a
			// turn is in flight can't accidentally apply to two turns.
			const bool start_new_session = pending_new_session;
			pending_new_session = false;

			if (chat_worker.joinable())
			{
				chat_worker.join();
			}
			chat_worker = droidcli::core::spawn(
				"tui.chat_worker",
				[run_chat_turn, message, start_new_session]() { run_chat_turn(message, start_new_session); },
				thread_event_sink);
			return true;
		}
		return false;
	});

	// Only connector_menu and chat_input are focusable (Menu/Input); Tab
	// cycles between them. Renderer-only panels (tasks/tools/log/chat_log)
	// never take focus. Each focusable component is wrapped in its own
	// single-child Renderer(component, fn) below so it stays the sole
	// focusable descendant of its pane - FTXUI's default ActiveChild()/
	// Focusable() walk just passes through a single-child wrapper, so
	// TakeFocus()/Focused() work the same as before despite the extra
	// nesting the resizable-split layout introduces.
	Component left_column = Renderer(connector_menu, [&]() -> Element
	{
		Element connectors_panel = window(text(" Connectors  (l=launch, s=stop) "), connector_menu->Render()) | flex;
		Element tasks_panel = window(text(" Tasks "), tasks_view->Render()) | flex;
		Element tools_panel = window(text(" Agent Tools "), tools_view->Render()) | flex;
		return vbox({ connectors_panel, tasks_panel, tools_panel });
	});

	Component chat_column = Renderer(chat_input, [&]() -> Element
	{
		return window(text(" Agent Chat  (Tab/Esc to leave, Enter to send) "),
			vbox({ chat_log_view->Render() | flex, separator(), chat_input->Render() }));
	});

	// App Log panel (watchdog transitions, scheduled/queued task dispatch,
	// connector/fs/run activity - see append_app_log in cli/host.cpp) - was
	// built but unmounted before the self-health/scheduler work (see "Phase
	// 9" in ARCHITECTURE.md), which needed somewhere for that activity to
	// actually be visible without a curl.
	Component right_column = Renderer([&]() -> Element
	{
		return window(text(" App Log "), log_view->Render());
	});

	// Mouse-draggable panes: left column | chat (middle, gets remaining
	// space) | right column. left_pane_width/right_pane_width are live state
	// FTXUI mutates directly as the user drags a divider - must outlive
	// screen.Loop() below, hence declared in this same function scope.
	Component split = chat_column;
	split = ResizableSplitLeft(left_column, split, &left_pane_width);
	split = ResizableSplitRight(right_column, split, &right_pane_width);

	Component full_ui = Renderer(split, [&]() -> Element
	{
		const std::string focus_hint = chat_input->Focused()
			? "chat focused - Tab/Esc: to connectors   Enter: send   Ctrl+C: quit anytime"
			: "connectors focused - Tab: to chat   q: quit   l: launch   s: stop   n: new session   j/k/arrows: move   y: copy chat   drag borders to resize";
		const std::string session_line = current_session_id.empty()
			? "session: (none yet - starts on your first message)"
			: "session: " + current_session_id;
		return vbox({
			text(status_line) | bold,
			text(session_line) | dim,
			split->Render() | flex,
			text(focus_hint) | dim,
		});
	});

	full_ui = CatchEvent(full_ui, [&](Event event) -> bool
	{
		if (event == Event::Custom)
		{
			{
				std::lock_guard<std::mutex> lock(polled.mutex);
				connectors = polled.connectors;
				tasks = polled.tasks;
				log_lines = polled.log_lines;
			}
			rebuild_connector_entries();

			std::lock_guard<std::mutex> chat_lock(chat_work.mutex);
			for (ChatEntry& entry : chat_work.pending_entries)
			{
				chat_entries.push_back(std::move(entry));
			}
			chat_work.pending_entries.clear();
			if (!chat_work.session_id.empty() && chat_work.session_id != current_session_id)
			{
				current_session_id = chat_work.session_id;
				write_last_session_id(current_session_id);
				chat_work.session_id.clear();
			}
			if (chat_work.has_pending_tool_call)
			{
				pending_tool_approval.active = true;
				// current_session_id was just updated above (if this response
				// carried one) - the pending call always belongs to whichever
				// session the turn that produced it just ran under.
				pending_tool_approval.session_id = current_session_id;
				pending_tool_approval.tool = chat_work.pending_tool_name;
				pending_tool_approval.arguments_json = chat_work.pending_tool_args;
				chat_work.has_pending_tool_call = false;
			}
			if (chat_work.clear_in_flight)
			{
				agent_turn_in_flight = false;
				chat_work.clear_in_flight = false;
			}
			return true;
		}

		if (event == Event::CtrlC)
		{
			running_flag = false;
			screen.Exit();
			return true;
		}

		// Explicit Tab/Shift+Tab/Escape focus switching, handled here (before
		// anything reaches chat_input) rather than relying on
		// Container::Vertical's built-in Tab-cycling: FTXUI's Input consumes
		// Tab as a literal character in its OnEvent (event.is_character() is
		// checked before anything else), so it never reaches the container's
		// own focus-navigation logic - without this, Tab just inserts a tab
		// character into the message and focus can never leave the input.
		if (event == Event::Tab || event == Event::TabReverse)
		{
			if (chat_input->Focused())
			{
				connector_menu->TakeFocus();
			}
			else
			{
				chat_input->TakeFocus();
			}
			return true;
		}
		if (event == Event::Escape && chat_input->Focused())
		{
			connector_menu->TakeFocus();
			return true;
		}

		// Single-letter connector shortcuts (q/l/s/j/k) must never fire while
		// the chat input has focus, or typing a message like "please" would
		// launch/stop/move the connector selection on every matching letter.
		// FTXUI tracks per-component focus as events flow through the tree;
		// chat_input->Focused() reflects whether Tab last landed there.
		if (chat_input->Focused())
		{
			return false;
		}

		if (event == Event::q)
		{
			running_flag = false;
			screen.Exit();
			return true;
		}

		if (event == Event::l || event == Event::s)
		{
			if (selected_connector >= 0 && selected_connector < static_cast<int>(connectors.size()))
			{
				const std::string connector_id = connectors[static_cast<size_t>(selected_connector)].id;
				// Blocking (process spawn / HTTP call), but only reached here, on
				// the FTXUI event thread in response to a keypress - never from
				// the background poller thread below.
				if (event == Event::l)
				{
					host.launch_connector(connector_id);
				}
				else
				{
					host.stop_connector(connector_id);
				}
			}
			return true;
		}

		if (event == Event::y)
		{
			const std::string transcript = format_chat_transcript(chat_entries);
			if (transcript.empty())
			{
				chat_entries.push_back(ChatEntry{"info", "Nothing to copy yet."});
			}
			else if (copy_text_to_clipboard(transcript))
			{
				chat_entries.push_back(ChatEntry{"info", "Copied the chat transcript to the clipboard."});
			}
			else
			{
				chat_entries.push_back(ChatEntry{"error", "Could not copy to the clipboard."});
			}
			return true;
		}

		if (event == Event::n)
		{
			// Takes effect on the *next* message (agent_turn requires a
			// non-empty message, so there's no way to clear the server-side
			// session immediately without one) - see start_new_session in
			// run_chat_turn above. The visual chat panel clears right away
			// so it's obvious a fresh conversation has started, even though
			// the old one is still on disk and resumable by session_id.
			pending_new_session = true;
			current_session_id.clear();
			chat_entries.clear();
			chat_entries.push_back(ChatEntry{"info", "Starting a new session on your next message."});
			return true;
		}

		return false;
	});

	// Greet the user unconditionally the moment the TUI opens, then - if
	// Ollama isn't ready yet - follow up with the same prompt
	// describe_setup_prompt() would give after a failed attempt (e.g. the
	// numbered list of already-pulled models), so "type a number to pick
	// one, or a name to pull a new one" is visible without typing anything
	// first. Safe to touch chat_entries directly here (single-threaded,
	// before screen.Loop() starts).
	chat_entries.push_back(ChatEntry{"info",
		"Welcome to droidcli " + std::string(version_string) + ". Type a message below and press Enter to chat."});

	// Resume the last session this TUI process was on, if any (see
	// current_session_id's declaration above and "Persistent memory" in
	// ARCHITECTURE.md) - build_agent_history_json() is a local SQLite read
	// (via DroidHost::memory_store_), safe to call synchronously here
	// before screen.Loop() starts, same as the Ollama setup check below.
	try
	{
		const std::string last_session_id = read_last_session_id();
		if (!last_session_id.empty())
		{
			const std::vector<ChatEntry> resumed = parse_agent_history_for_resume(
				host.build_agent_history_json(last_session_id));
			if (!resumed.empty())
			{
				current_session_id = last_session_id;
				for (const ChatEntry& entry : resumed)
				{
					chat_entries.push_back(entry);
				}
			}
		}
	}
	catch (const std::exception& e)
	{
		chat_entries.push_back(ChatEntry{"error", std::string("internal error: ") + e.what()});
	}
	catch (...)
	{
		chat_entries.push_back(ChatEntry{"error", "internal error: unknown exception"});
	}

	try
	{
		const OllamaSetupStatus initial_status = parse_ollama_setup_status(host.ollama_setup_status_json());
		const OllamaSetupState initial_state = compute_setup_state(initial_status);
		const std::string prompt = describe_setup_prompt(initial_state, initial_status);
		if (!prompt.empty())
		{
			chat_entries.push_back(ChatEntry{"info", prompt});
		}
	}
	catch (const std::exception& e)
	{
		chat_entries.push_back(ChatEntry{"error", std::string("internal error: ") + e.what()});
	}
	catch (...)
	{
		chat_entries.push_back(ChatEntry{"error", "internal error: unknown exception"});
	}

	// An exception escaping a std::thread's entry function calls
	// std::terminate() immediately - it does NOT propagate to poller.join()
	// below, so a try/catch around screen.Loop() would not protect this
	// thread. Defense in depth: catch here too, log to stderr, and keep
	// polling rather than letting one bad iteration kill the process.
	std::thread poller = droidcli::core::spawn("tui.poller", [&]()
	{
		while (running_flag)
		{
			try
			{
				std::vector<ConnectorRow> next_connectors = parse_connectors(host.list_connectors_json());
				for (ConnectorRow& row : next_connectors)
				{
					row.live_status = summarize_status(row.kind, host.connector_status_json(row.id));
				}
				std::vector<TaskRow> next_tasks = parse_tasks(host.list_tasks_json());
				std::vector<LogRow> next_log = parse_log_lines(host.build_app_log_json());

				{
					std::lock_guard<std::mutex> lock(polled.mutex);
					polled.connectors = std::move(next_connectors);
					polled.tasks = std::move(next_tasks);
					polled.log_lines = std::move(next_log);
				}

				// FTXUI's documented pattern for live-updating dashboards: nudge the
				// event loop with a Custom event so it redraws even with no keypress.
				screen.PostEvent(Event::Custom);
			}
			catch (const std::exception& e)
			{
				std::cerr << "droidcli: TUI poller error: " << e.what() << std::endl;
			}
			catch (...)
			{
				std::cerr << "droidcli: TUI poller error: unknown exception" << std::endl;
			}

			for (int waited_ms = 0; waited_ms < 500 && running_flag; waited_ms += 100)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
	}, thread_event_sink);

	// Defense in depth, matching the try/catch already wrapped around the
	// chat-submit handler above: nothing reachable from screen.Loop() is
	// expected to throw, but if it ever did, an uncaught exception here would
	// unwind past main() and abort the whole process instead of just this
	// screen. Catch, log, and fall through to the normal shutdown path.
	try
	{
		screen.Loop(full_ui);
	}
	catch (const std::exception& e)
	{
		std::cerr << "droidcli: TUI loop error: " << e.what() << std::endl;
	}
	catch (...)
	{
		std::cerr << "droidcli: TUI loop error: unknown exception" << std::endl;
	}

	running_flag = false;
	if (poller.joinable())
	{
		poller.join();
	}
	// Must join before `screen` goes out of scope below - a still-running
	// chat_worker calls screen.PostEvent() when it finishes, which would be
	// use-after-free against a destroyed ScreenInteractive otherwise. If a
	// slow install/pull is in flight this can make quitting wait for it;
	// that's an acceptable tradeoff over a crash.
	if (chat_worker.joinable())
	{
		chat_worker.join();
	}

	return 0;
}

} // namespace droidcli::cli
