#include "tui.hpp"

#include "clipboard.hpp"
#include "net/json.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
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

// "HH:MM:SS", matching DroidHost::make_log_timestamp()'s format (the one
// already shown in log_view/tools_view/apps_view) - that method is private to
// DroidHost, so the chat panel gets its own copy rather than exposing it.
std::string current_time_hms()
{
	const std::time_t raw_time = std::time(nullptr);
	std::tm local_time {};
#if defined(_WIN32)
	localtime_s(&local_time, &raw_time);
#else
	localtime_r(&raw_time, &local_time);
#endif
	char buffer[16] {};
	std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local_time);
	return buffer;
}

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

// One remembered name -> path mapping from DroidHost::list_known_locations_json()
// (see KnownLocation, cli/memory_store.hpp) - the Locations panel's second
// bullet group, below the system-locations one.
struct LocationRow {
	std::string name;
	std::string resolved_path;
	std::string updated_at;
};

// One system-level location (cwd, Desktop, Home, Documents, Downloads,
// Program Files - see SystemInfo, cli/system_info.hpp) - "where we are and
// what this machine looks like," not something the model explicitly
// remembered. Same {name, path} shape as LocationRow so both render as
// identical bullets in the Locations panel; kept as a separate struct/list
// since these two groups have different provenance (a live OS query vs. a
// persisted memory) worth keeping visually distinct.
struct LocationEntry {
	std::string name;
	std::string path;
};

// Parsed GET /api/locations response: {"ok":true,"cwd":...,"desktop_path":...,
// "system_locations":[{"name":...,"path":...}],
// "known_locations":[{"name":...,"resolved_path":...,"updated_at":...}]}.
struct LocationsSnapshot {
	std::vector<LocationEntry> system_locations;
	std::vector<LocationRow> remembered;
};

LocationsSnapshot parse_locations(const std::string& json)
{
	LocationsSnapshot snapshot;
	const std::string cwd = net::extract_json_string_field(json, "cwd");
	if (!cwd.empty())
	{
		snapshot.system_locations.push_back(LocationEntry{"Current Directory", cwd});
	}
	const std::string desktop_path = net::extract_json_string_field(json, "desktop_path");
	if (!desktop_path.empty())
	{
		snapshot.system_locations.push_back(LocationEntry{"Desktop", desktop_path});
	}
	for (const std::string& object : extract_json_object_array(json, "system_locations"))
	{
		LocationEntry entry;
		entry.name = net::extract_json_string_field(object, "name");
		entry.path = net::extract_json_string_field(object, "path");
		if (!entry.name.empty())
		{
			snapshot.system_locations.push_back(entry);
		}
	}
	for (const std::string& object : extract_json_object_array(json, "known_locations"))
	{
		LocationRow row;
		row.name = net::extract_json_string_field(object, "name");
		row.resolved_path = net::extract_json_string_field(object, "resolved_path");
		row.updated_at = net::extract_json_string_field(object, "updated_at");
		if (!row.name.empty())
		{
			snapshot.remembered.push_back(row);
		}
	}
	return snapshot;
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
	return channel == "run" || channel == "ffmpeg" || channel == "open" || channel == "process";
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
	LocationsSnapshot locations;
};

// One line of the chat panel: who said it (drives color/weight) and the text.
// timestamp defaults to "now" (DroidHost::make_log_timestamp(), the same
// HH:MM:SS used by the App Log/Agent Tools/Apps panels) so every existing
// ChatEntry{role, text} call site gets a timestamp for free, taken at the
// moment the entry is actually constructed. Explicit constructors (not a
// default member initializer on an aggregate) - MSVC rejects a partial-brace
// aggregate init ({role, text}, timestamp defaulted) as "invalid aggregate
// initialization" even though it's valid since C++14; constructors sidestep
// the conformance gap entirely while keeping every existing two-arg call site
// unchanged.
struct ChatEntry {
	std::string role; // "user" | "assistant" | "thinking" | "tool" | "error" | "info"
	std::string text;
	std::string timestamp;

	ChatEntry(std::string role_, std::string text_)
		: role(std::move(role_)), text(std::move(text_)), timestamp(current_time_hms()) {}
	ChatEntry(std::string role_, std::string text_, std::string timestamp_)
		: role(std::move(role_)), text(std::move(text_)), timestamp(std::move(timestamp_)) {}
};

// Plain-text rendering of the chat transcript (one line per entry, with the
// same [USER]/[AGENT]/[SYSTEM] prefixes the chat panel shows, minus color)
// for copying to the clipboard.
std::string format_chat_transcript(const std::vector<ChatEntry>& entries)
{
	std::ostringstream stream;
	for (const ChatEntry& entry : entries)
	{
		const std::string ts_prefix = "[" + entry.timestamp + "] ";
		if (entry.role == "user")
		{
			stream << ts_prefix << "[USER] " << entry.text << "\n";
		}
		else if (entry.role == "assistant")
		{
			stream << ts_prefix << "[AGENT] " << entry.text << "\n";
		}
		else if (entry.role == "thinking")
		{
			stream << ts_prefix << "[AGENT] [THINKING] " << entry.text << "\n";
		}
		else if (entry.role == "tool")
		{
			stream << ts_prefix << "[AGENT] [EXECUTION] " << entry.text << "\n";
		}
		else if (entry.role == "error")
		{
			stream << ts_prefix << "error: " << entry.text << "\n";
		}
		else
		{
			stream << ts_prefix << "[SYSTEM] " << entry.text << "\n";
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

	// DroidHost::classify_via_llm's own chain-of-thought (see "The LLM
	// provider" in ARCHITECTURE.md) - shown first, since it's what actually
	// drove whichever tool call/reply follows. Never the assistant's real
	// reply - a distinct role/line so it can never be mistaken for one.
	const std::string thinking = net::extract_json_string_field(json, "thinking");
	if (!thinking.empty())
	{
		entries.push_back(ChatEntry{"thinking", thinking});
	}

	for (const std::string& action : extract_json_object_array(json, "actions"))
	{
		const std::string tool = net::extract_json_string_field(action, "tool");
		const std::string args = net::extract_json_string_field(action, "arguments_json");
		const std::string result = net::extract_json_string_field(action, "result_json");
		// The raw tool result (list_windows_locations' full candidate list,
		// find_application's matches, etc.) used to only reach the App Log
		// panel via DroidHost::append_app_log's own "chat" channel line -
		// Agent Chat showed just the call, never what it actually returned,
		// so a request answered from a long candidate list looked like it
		// vanished into a vague "returned a comprehensive list" summary with
		// no way to see the list itself without switching panels.
		entries.push_back(ChatEntry{"tool",
			"called " + tool + "(" + args + ")" + (result.empty() ? "" : " -> " + result)});
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
		bool looks_destructive = false;
		net::extract_json_bool_field(pending_object, "looks_destructive", looks_destructive);
		// A visibility aid, not a gate (cli/host.cpp's looks_like_destructive_command -
		// "Phase 26" - never blocks anything on its own) - a human skimming
		// past a routine "yes" is more likely to actually stop and read this
		// particular prompt with the warning prefixed.
		const std::string prefix = looks_destructive ? "[!! DESTRUCTIVE !!] " : "";
		entries.push_back(ChatEntry{"info",
			prefix + "[AGENT WANTS TO] " + out_pending_tool_name + "(" + out_pending_tool_args + ") - approve? (yes/no, or say why not)"});
		return entries;
	}

	const std::string assistant = net::extract_json_string_field(json, "assistant");
	entries.push_back(ChatEntry{"assistant", assistant.empty() ? "(no reply)" : assistant});
	return entries;
}

// Mirrors DroidHost::agent_turn()/agent_tool_decision()'s "pending_tool_call"
// field - set whenever the agent's tool-calling loop pauses on a
// side-effecting tool (run_command, run_ffmpeg, write_file,
// open_application, launch/stop_connector, enqueue_task; see
// tool_call_requires_approval in cli/host.cpp) instead of running it. The
// next Enter is treated as the yes/no (or free-text decline reason) that
// resolves it, not as a new chat message.
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

// Copies `text` (UTF-8) to the OS clipboard - now a thin wrapper over
// cli::write_text_to_clipboard (clipboard.hpp/.cpp), shared with the
// read_clipboard/write_clipboard agent tools (Phase 15, ARCHITECTURE.md)
// rather than a second, TUI-only implementation of the same Win32 calls.
bool copy_text_to_clipboard(const std::string& text)
{
	return write_text_to_clipboard(text).ok;
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
		// created_at is stored/reported as "YYYY-MM-DD HH:MM:SS"
		// (MemoryStore, DroidHost::make_full_log_timestamp) - the trailing 8
		// characters are the HH:MM:SS this panel actually displays, so a
		// resumed message shows when it really happened instead of the
		// session-resume time ChatEntry's default ("now") would give it.
		const std::string created_at = net::extract_json_string_field(message, "created_at");
		const std::string resumed_timestamp = created_at.size() >= 8
			? created_at.substr(created_at.size() - 8)
			: current_time_hms();
		if (role == "assistant")
		{
			entries.push_back(ChatEntry{"assistant", content, resumed_timestamp});
		}
		else if (role == "tool")
		{
			entries.push_back(ChatEntry{"tool", "(resumed) " + content, resumed_timestamp});
		}
		else
		{
			entries.push_back(ChatEntry{"user", content, resumed_timestamp});
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

// A window() title styled with a light-blue background/dark text - shared by
// every panel title (Connectors, Tasks, Agent Tools, Apps, Locations, Agent
// Chat, App Log) so they read as one consistent set of chrome alongside the
// top status line and bottom focus-hint bar, which get the same treatment
// directly in run_tui below. window() takes an Element for its title
// argument (not just plain text), so this slots in with no other changes to
// how each panel is built.
ftxui::Element panel_title(const std::string& label)
{
	return ftxui::text(label) | ftxui::bgcolor(ftxui::Color::LightSkyBlue1) | ftxui::color(ftxui::Color::Black);
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
	std::vector<LogRow> log_lines;
	LocationsSnapshot locations;
	std::vector<ChatEntry> chat_entries;
	// Resizable-split pane widths (columns) - mutated directly by
	// ResizableSplitLeft/Right on mouse drag, see the split construction
	// below. Both remain user-draggable at any time; only the *starting*
	// value of right_pane_width is computed below (half of whatever's left
	// after the left column), rather than a fixed guess that only looked
	// right at one particular terminal size.
	int left_pane_width = 36;
	// Terminal::Size() is a plain OS query - safe to call before screen.Loop()
	// starts. The -2 accounts for the two single-column separators
	// ResizableSplitLeft and ResizableSplitRight each draw between panes, so
	// Agent Chat and App Log actually split the true remaining width evenly,
	// not the remaining width plus two separator columns that aren't really
	// available to either of them.
	const int terminal_width = Terminal::Size().dimx;
	int right_pane_width = std::max(0, terminal_width - left_pane_width - 2) / 2;
	std::string chat_input_text;
	// Cursor position for chat_input, exposed so history recall below can
	// jump it to the end of a recalled message instead of leaving it
	// wherever it happened to be - see the InputOption passed to Input().
	int chat_input_cursor = 0;
	// Shell-style message history: every message actually sent to the agent
	// (not pending_tool_approval yes/no replies - those aren't "prompts"
	// worth recalling), oldest first. ArrowUp/ArrowDown below walk
	// it; chat_history_cursor is -1 while editing live text, otherwise an
	// index into chat_history. chat_history_draft holds whatever was being
	// typed before the first ArrowUp, restored once ArrowDown walks back
	// past the newest entry - the same behavior a shell's history gives you.
	std::vector<std::string> chat_history;
	int chat_history_cursor = -1;
	std::string chat_history_draft;
	bool agent_turn_in_flight = false;
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

	// Every entry that lands in the chat pane should also land in the durable
	// log (logs/log.jsonl, git-ignored - see logs/README.md), not just the
	// half of the conversation that happens to round-trip through
	// agent_turn()/agent_tool_decision() (those already log themselves inside
	// DroidHost). Approval-flow replies, "Approved."/"Cancelled." banners,
	// clipboard feedback, session banners, and caught-exception messages were
	// previously chat_entries.push_back-only - visible on screen, invisible in
	// logs/history. Use this instead of chat_entries.push_back directly for
	// any TUI-local entry (an entry built from an already-logged agent_turn/
	// agent_tool_decision response, e.g. parse_chat_response's output, should
	// keep using push_back as before - logging it again here would duplicate
	// the same content under two log lines).
	auto push_chat_entry = [&](const std::string& role, const std::string& text)
	{
		host.log_chat_entry(role, text);
		chat_entries.push_back(ChatEntry{role, text});
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

	// A live activity terminal, not the static full tool catalog (GET
	// /api/agent/tools still exists and is the canonical list for anyone
	// who wants it - see ARCHITECTURE.md) - only tools actually invoked this
	// session, most recent at the bottom, derived from the same log_lines
	// the App Log panel already polls. A real tool execution is logged as
	// "tool <name>(...) -> ..." under the "chat" channel (see
	// execute_agent_tool call sites in cli/host.cpp) - this filters to
	// exactly that line shape and pulls out just the name, so this panel
	// answers "what has the agent actually run" at a glance instead of
	// listing everything it's theoretically capable of.
	Component tools_view = Renderer([&]() -> Element
	{
		Elements rows;
		for (const LogRow& row : log_lines)
		{
			if (row.channel != "chat")
			{
				continue;
			}
			const bool declined = row.summary.rfind("tool declined ", 0) == 0;
			const bool executed = !declined && row.summary.rfind("tool ", 0) == 0;
			if (!declined && !executed)
			{
				continue;
			}
			const std::string prefix = declined ? "tool declined " : "tool ";
			const std::string rest = row.summary.substr(prefix.size());
			const size_t paren = rest.find('(');
			const std::string tool_name = paren == std::string::npos ? rest : rest.substr(0, paren);
			// paragraph() (not text()) on the name so a long tool name wraps
			// within the panel's width instead of overflowing off the right
			// edge, matching log_view/chat_log_view.
			rows.push_back(hbox({
				text("[" + row.timestamp + "] ") | dim,
				paragraph(tool_name) | bold | color(row.success ? Color::Green : Color::Red),
			}));
		}
		if (rows.empty())
		{
			rows.push_back(text("(no tools used yet this session)") | dim);
		}
		else
		{
			// Keep pinned to the newest entry, same rationale as log_view.
			rows.back() |= focus;
		}
		return vbox(rows) | yframe | flex;
	});

	// Same "live terminal from the log" approach as tools_view above, for
	// apps droidcli has launched or otherwise controls: open_application
	// calls ("open" channel), launched_process connector launch/stop
	// ("process" channel).
	Component apps_view = Renderer([&]() -> Element
	{
		Elements rows;
		for (const LogRow& row : log_lines)
		{
			std::string label;
			if (row.channel == "open" || row.channel == "process")
			{
				label = row.summary;
			}
			else
			{
				continue;
			}
			// paragraph() (not text()) on the label so a long app/command label
			// wraps within the panel's width instead of overflowing off the
			// right edge, matching log_view/chat_log_view.
			rows.push_back(hbox({
				text("[" + row.timestamp + "] ") | dim,
				paragraph(label) | color(row.success ? Color::Green : Color::Red),
			}));
		}
		if (rows.empty())
		{
			rows.push_back(text("(no apps launched yet this session)") | dim);
		}
		else
		{
			rows.back() |= focus;
		}
		return vbox(rows) | yframe | flex;
	});

	// "Views of the system" panel (Phase 19, ARCHITECTURE.md): "where we are
	// right now" (cwd, the user's real Desktop) plus every name -> path the
	// model has remembered via remember_location, most recently updated
	// first - a live, inspectable answer to "what does droidcli think it
	// knows about the filesystem" without having to call get_known_locations
	// through chat. Polled the same way as tasks_view/connector_menu (see the
	// poller thread below), not derived from log_lines like tools_view/
	// apps_view/log_view - this reflects current stored state, not a stream
	// of past events.
	Component locations_view = Renderer([&]() -> Element
	{
		Elements rows;
		// One bullet per location: a short "* Name" line (bold, so it reads
		// at a glance), then the real path indented on the line(s) below it -
		// paragraph() (not text()) wraps a long path within the panel's width
		// instead of overflowing off the right edge, matching every other
		// panel in this file. Name first/small, path below/potentially much
		// longer, rather than one "Name: path" line that gets unreadable the
		// moment the path is long.
		auto add_location_bullet = [&](const std::string& name, const std::string& path, Color name_color)
		{
			rows.push_back(text("* " + name) | bold | color(name_color));
			rows.push_back(paragraph("  " + path) | dim);
		};

		for (const LocationEntry& entry : locations.system_locations)
		{
			add_location_bullet(entry.name, entry.path, Color::White);
		}
		if (!locations.system_locations.empty())
		{
			rows.push_back(separator());
		}
		if (locations.remembered.empty())
		{
			rows.push_back(text("(no locations remembered yet)") | dim);
		}
		else
		{
			for (const LocationRow& location : locations.remembered)
			{
				add_location_bullet(location.name, location.resolved_path, Color::Cyan);
			}
		}
		return vbox(rows) | yframe | flex;
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
	//  - watchdog/task/thread channels each get their own distinct color so
	//    the high-volume chat channel doesn't drown them out visually.
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
				// A real color (not just dim), matching every other channel's
				// treatment here, so thread spawn/join lines are as visually
				// distinguishable from the rest of the log as watchdog/task/
				// process-launch lines already are.
				element |= color(Color::Blue);
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
			// of overflowing off the right edge. Timestamp prefix matches the
			// "[HH:MM:SS] " style already used by log_view/tools_view/apps_view.
			const std::string ts_prefix = "[" + entry.timestamp + "] ";
			if (entry.role == "user")
			{
				lines.push_back(paragraph(ts_prefix + "[USER] " + entry.text) | bold | color(Color::Blue));
			}
			else if (entry.role == "assistant")
			{
				lines.push_back(paragraph(ts_prefix + "[AGENT] " + entry.text) | color(Color::Green));
			}
			else if (entry.role == "thinking")
			{
				// Same green as a real assistant reply (it's the same agent,
				// same turn) but dimmed - visibly distinct from the actual
				// reply immediately below it, never mistaken for one.
				lines.push_back(paragraph(ts_prefix + "[AGENT] [THINKING] " + entry.text) | dim | color(Color::Green));
			}
			else if (entry.role == "tool")
			{
				lines.push_back(paragraph(ts_prefix + "[AGENT] [EXECUTION] " + entry.text) | dim | color(Color::Yellow));
			}
			else if (entry.role == "error")
			{
				lines.push_back(paragraph(ts_prefix + "error: " + entry.text) | color(Color::Red));
			}
			else
			{
				// [SYSTEM] (Ollama setup prompts, etc.) shows up often - an
				// explicit dark grey keeps it visually de-emphasized without
				// relying on `dim`, whose actual look varies by terminal.
				lines.push_back(paragraph(ts_prefix + "[SYSTEM] " + entry.text) | color(Color::GrayDark));
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
					push_chat_entry("user", reply);
				}
				const std::string lower_reply = to_lower(reply);
				const bool approved = lower_reply == "yes" || lower_reply == "y";
				const bool bare_decline = lower_reply == "no" || lower_reply == "n" || lower_reply == "cancel";
				const std::string reason = (approved || bare_decline) ? std::string() : reply;
				const std::string decision_session_id = pending_tool_approval.session_id;

				push_chat_entry("info", approved ? "Approved. Running..." : "Declined.");

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
				// Not push_chat_entry: this message is about to be sent to
				// agent_turn() (run_chat_turn below), which logs "user: <message>"
				// itself the moment it runs - logging it again here first would
				// duplicate the same line under two timestamps.
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
		// All five panels share equal flex weight, so "Agent Tools" is
		// exactly as tall as "Tasks" (and Connectors/Apps/Locations) rather
		// than whatever FTXUI's default content-driven sizing would give it.
		Element connectors_panel = window(panel_title(" Connectors  (l=launch, s=stop) "), connector_menu->Render()) | flex;
		Element tasks_panel = window(panel_title(" Tasks "), tasks_view->Render()) | flex;
		Element tools_panel = window(panel_title(" Agent Tools "), tools_view->Render()) | flex;
		Element apps_panel = window(panel_title(" Apps "), apps_view->Render()) | flex;
		Element locations_panel = window(panel_title(" Locations "), locations_view->Render()) | flex;
		return vbox({ connectors_panel, tasks_panel, tools_panel, apps_panel, locations_panel });
	});

	Component chat_column = Renderer(chat_input, [&]() -> Element
	{
		return window(panel_title(" Agent Chat  (Tab/Esc to leave, Enter to send) "),
			vbox({ chat_log_view->Render() | flex, separator(), chat_input->Render() }));
	});

	// App Log panel (watchdog transitions, scheduled/queued task dispatch,
	// connector/fs/run activity - see append_app_log in cli/host.cpp) - was
	// built but unmounted before the self-health/scheduler work (see "Phase
	// 9" in ARCHITECTURE.md), which needed somewhere for that activity to
	// actually be visible without a curl.
	Component right_column = Renderer([&]() -> Element
	{
		return window(panel_title(" App Log "), log_view->Render());
	});

	// Mouse-draggable panes: left column | chat (middle) | right column (App
	// Log). Both dividers are user-draggable at any time via
	// left_pane_width/right_pane_width, same as before - only
	// right_pane_width's *starting* value changed (see its computation
	// above): an actual half-of-what's-left-of-the-terminal split instead of
	// a fixed guess, while staying exactly where the user leaves it
	// afterward, same as the left column already does.
	Component middle = chat_column;
	middle = ResizableSplitRight(right_column, middle, &right_pane_width);
	Component split = ResizableSplitLeft(left_column, middle, &left_pane_width);

	Component full_ui = Renderer(split, [&]() -> Element
	{
		const std::string focus_hint = chat_input->Focused()
			? "chat focused - Tab/Esc: to connectors   Enter: send   Ctrl+C: quit anytime"
			: "connectors focused - Tab: to chat   q: quit   l: launch   s: stop   n: new session   j/k/arrows: move   y: copy chat   drag borders to resize";
		const std::string active_model = host.active_model_name();
		const std::string session_line = (current_session_id.empty()
			? "session: (none yet - starts on your first message)"
			: "session: " + current_session_id)
			+ " | model: " + (active_model.empty() ? "none" : active_model);
		// Light-blue background/dark text on the top status line and bottom
		// focus-hint line, matching panel_title() above - xflex (X-axis only)
		// stretches the colored box across the full terminal width without
		// also claiming vertical space from the split in the middle: plain
		// flex expands on both axes, which starved the split of height and
		// blew up these two lines to fill the whole screen instead.
		return vbox({
			text(status_line) | bold | bgcolor(Color::LightSkyBlue1) | color(Color::Black) | xflex,
			text(session_line) | dim,
			split->Render() | flex,
			text(focus_hint) | bgcolor(Color::LightSkyBlue1) | color(Color::Black) | xflex,
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
				locations = polled.locations;
			}
			rebuild_connector_entries();

			// Not push_chat_entry: pending_entries are parsed straight out of
			// an agent_turn()/agent_tool_decision() response body, and that
			// call already logged itself server-side (DroidHost's own
			// append_app_log calls in classify_via_llm/execute_decision_or_pause/
			// finish_turn_after_execution) - logging here too would duplicate
			// each assistant/tool line under a second timestamp.
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
				push_chat_entry("info", "Nothing to copy yet.");
			}
			else if (copy_text_to_clipboard(transcript))
			{
				push_chat_entry("info", "Copied the chat transcript to the clipboard.");
			}
			else
			{
				push_chat_entry("error", "Could not copy to the clipboard.");
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
			push_chat_entry("info", "Starting a new session on your next message.");
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
	push_chat_entry("info",
		"Welcome to droidcli " + std::string(version_string) + ". Type a message below and press Enter to chat.");

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
					// Not push_chat_entry: this content is already in
					// MemoryStore/logs/log.jsonl from when it was first said -
					// re-logging it here on every resume would duplicate the
					// same conversation turn under a new timestamp each time
					// the TUI restarts.
					chat_entries.push_back(entry);
				}
			}
		}
	}
	catch (const std::exception& e)
	{
		push_chat_entry("error", std::string("internal error: ") + e.what());
	}
	catch (...)
	{
		push_chat_entry("error", "internal error: unknown exception");
	}

	try
	{
		const OllamaSetupStatus initial_status = parse_ollama_setup_status(host.ollama_setup_status_json());
		const OllamaSetupState initial_state = compute_setup_state(initial_status);
		const std::string prompt = describe_setup_prompt(initial_state, initial_status);
		if (!prompt.empty())
		{
			push_chat_entry("info", prompt);
		}
	}
	catch (const std::exception& e)
	{
		push_chat_entry("error", std::string("internal error: ") + e.what());
	}
	catch (...)
	{
		push_chat_entry("error", "internal error: unknown exception");
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
				LocationsSnapshot next_locations = parse_locations(host.list_known_locations_json());

				{
					std::lock_guard<std::mutex> lock(polled.mutex);
					polled.connectors = std::move(next_connectors);
					polled.tasks = std::move(next_tasks);
					polled.log_lines = std::move(next_log);
					polled.locations = std::move(next_locations);
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
