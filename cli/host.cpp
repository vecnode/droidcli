#include "host.hpp"

#include "app_index.hpp"
#include "command_runner.hpp"
#include "intent/open_intent.hpp"
#include "intent/pending_command.hpp"
#include "reliability/claim_guards.hpp"
#include "reliability/command_guards.hpp"
#include "reliability/path_guards.hpp"
#include "settings_store.hpp"
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
#include <utility>

namespace droidcli::cli {
namespace {

// The path/content and claim/response guards used throughout this file now
// live in src/reliability/ (portable core, unit-tested in tests/ - see
// path_guards_test.cpp/claim_guards_test.cpp) rather than as local copies
// here, so a future edit can't silently regress one of them without a test
// failing.
using namespace droidcli::reliability;

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

// A real transcript showed "Sound Settings" (and, separately, "Recycle
// Bin") fail to open: neither is an installed application - neither has an
// Add/Remove Programs entry nor a discoverable .exe by that name, so
// find_installed_app_match/collect_installed_app_matches above will never
// find them, and a literal CreateProcess("Sound Settings") or
// CreateProcess("Recycle Bin") can never succeed no matter how it's spelled.
// This is droidcli's explicit, hand-maintained knowledge of Windows *itself*
// - built-in OS locations and Settings pages that exist on every Windows
// install regardless of what the user has installed - checked in
// try_quick_open_json only after the installed-apps index comes up empty,
// so a real installed app of the same name (e.g. a third-party "Sound
// Recorder") is never shadowed by this table.
//
// Every entry resolves to a real executable + a plain argument CreateProcess
// can actually launch - no ShellExecute needed: explorer.exe accepts a
// shell:/ms-settings: URI as an ordinary command-line argument and hands it
// to the shell itself, and control.exe/taskmgr.exe/mmc.exe are ordinary
// executables. Narrow and evidence-driven, not an attempt at an exhaustive
// Windows settings/CLSID list - entries added here should trace back to an
// observed "the model tried to open X and it failed" case, the same
// discipline every other guard in this file follows.
struct WellKnownWindowsTarget
{
	core::String display_name;
	core::String path_or_name;
	core::String args;
};

struct WellKnownWindowsTargetEntry
{
	const char* alias;
	const char* display_name;
	const char* path_or_name;
	const char* args;
};

// Deliberately widened past the original "one incident, one entry" rule
// (see the comment above WellKnownWindowsTarget) by direct user request,
// after a real transcript showed "the Windows panel that shows memory
// usage" fail: Task Manager was already in this table, but only under the
// single alias "task manager", and this matcher works by substring
// containment (see find_well_known_windows_target below) - a description
// that never contains that literal alias text can never match no matter how
// obviously it refers to the same thing. The fix here is two-part: more
// aliases per target (multiple rows sharing the same display_name/
// path_or_name/args, since WellKnownWindowsTargetEntry only carries one
// alias each), and more targets, covering the built-in Windows panels a
// user is likely to ask for by description rather than by their exact
// Microsoft-assigned name. Still every entry resolves to a real executable +
// argument CreateProcess can actually launch - see that comment for why no
// ShellExecute/CLSID machinery is needed. Also the backing data for
// list_windows_locations (below) - the agent tool that lets the model
// answer "what Windows panels can you open" from real data instead of
// guessing, the other half of the same incident (see "Windows panel
// awareness" in ARCHITECTURE.md).
static const WellKnownWindowsTargetEntry kWellKnownWindowsTargets[] = {
	{"recycle bin", "Recycle Bin", "explorer.exe", "shell:RecycleBinFolder"},
	{"sound settings", "Sound Settings", "explorer.exe", "ms-settings:sound"},
	{"display settings", "Display Settings", "explorer.exe", "ms-settings:display"},
	{"network settings", "Network Settings", "explorer.exe", "ms-settings:network"},
	{"bluetooth settings", "Bluetooth Settings", "explorer.exe", "ms-settings:bluetooth"},
	{"windows update", "Windows Update", "explorer.exe", "ms-settings:windowsupdate"},
	{"windows settings", "Windows Settings", "explorer.exe", "ms-settings:"},
	{"control panel", "Control Panel", "control.exe", ""},
	{"task manager", "Task Manager", "taskmgr.exe", ""},
	{"process manager", "Task Manager", "taskmgr.exe", ""},
	{"memory usage", "Task Manager", "taskmgr.exe", ""},
	{"memory panel", "Task Manager", "taskmgr.exe", ""},
	{"cpu usage", "Task Manager", "taskmgr.exe", ""},
	{"running processes", "Task Manager", "taskmgr.exe", ""},
	{"resource monitor", "Resource Monitor", "resmon.exe", ""},
	{"performance monitor", "Performance Monitor", "perfmon.exe", ""},
	{"device manager", "Device Manager", "mmc.exe", "devmgmt.msc"},
	{"disk management", "Disk Management", "mmc.exe", "diskmgmt.msc"},
	{"disk partition", "Disk Management", "mmc.exe", "diskmgmt.msc"},
	{"partition manager", "Disk Management", "mmc.exe", "diskmgmt.msc"},
	{"manage disks", "Disk Management", "mmc.exe", "diskmgmt.msc"},
	{"services", "Services", "mmc.exe", "services.msc"},
	{"event viewer", "Event Viewer", "eventvwr.exe", ""},
	{"system information", "System Information", "msinfo32.exe", ""},
	{"registry editor", "Registry Editor", "regedit.exe", ""},
	{"system properties", "System Properties", "control.exe", "system"},
	{"programs and features", "Programs and Features", "control.exe", "appwiz.cpl"},
	{"network connections", "Network Connections", "control.exe", "ncpa.cpl"},
	{"power options", "Power Options", "control.exe", "powercfg.cpl"},
	{"date and time", "Date and Time", "control.exe", "timedate.cpl"},
	{"storage settings", "Storage Settings", "explorer.exe", "ms-settings:storagesense"},
	{"about this pc", "About", "explorer.exe", "ms-settings:about"},
	{"apps and features", "Apps & Features", "explorer.exe", "ms-settings:appsfeatures"},
	{"windows security", "Windows Security", "explorer.exe", "windowsdefender:"},
	{"windows defender", "Windows Security", "explorer.exe", "windowsdefender:"},
	{"printers and scanners", "Printers & Scanners", "explorer.exe", "ms-settings:printers"},
	{"this pc", "This PC", "explorer.exe", "shell:MyComputerFolder"},
	{"my computer", "This PC", "explorer.exe", "shell:MyComputerFolder"},
	{"downloads folder", "Downloads", "explorer.exe", "shell:Downloads"},
};

bool find_well_known_windows_target(const core::String& query, WellKnownWindowsTarget& out)
{
	const core::String normalized_query = normalize_for_match(query);
	if (normalized_query.empty())
	{
		return false;
	}

	for (const WellKnownWindowsTargetEntry& target : kWellKnownWindowsTargets)
	{
		const core::String normalized_alias = normalize_for_match(target.alias);
		if (normalized_alias == normalized_query
			|| normalized_alias.find(normalized_query) != core::String::npos
			|| normalized_query.find(normalized_alias) != core::String::npos)
		{
			out.display_name = target.display_name;
			out.path_or_name = target.path_or_name;
			out.args = target.args;
			return true;
		}
	}
	return false;
}

// Backs the list_windows_locations agent tool - every distinct display_name
// in kWellKnownWindowsTargets, deduplicated (several rows above share one
// display_name across multiple aliases, e.g. Task Manager). Gives the model
// real data to answer "what Windows panels/settings can you open" instead
// of fabricating an answer - see the incident note above
// kWellKnownWindowsTargets.
core::String list_well_known_windows_targets_json()
{
	core::Array<core::String> seen_names;
	std::ostringstream stream;
	stream << '{' << net::json_bool_field("ok", true) << ",\"locations\":[";
	bool first = true;
	for (const WellKnownWindowsTargetEntry& target : kWellKnownWindowsTargets)
	{
		const core::String display_name = target.display_name;
		bool duplicate = false;
		for (const core::String& existing : seen_names)
		{
			if (existing == display_name)
			{
				duplicate = true;
				break;
			}
		}
		if (duplicate)
		{
			continue;
		}
		seen_names.push_back(display_name);

		if (!first)
		{
			stream << ',';
		}
		first = false;
		stream << '"' << net::escape_json_string(display_name) << '"';
	}
	stream << "]}";
	return stream.str();
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

// int32_t-scoped field extractor for host-local uses (timeout_ms, max_bytes)
// that never need more than 32 bits. net::extract_json_int_field (added
// alongside Task::scheduled_for_ms) is the int64_t-scoped equivalent for
// fields that can hold an absolute epoch-ms value; kept separate rather than
// widening every call site here to int64_t for no benefit.
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

// Replaces the value of an existing top-level "field_name":"..." string
// field in `json` with `new_value`, re-escaped - returns `json` unchanged if
// the field isn't present or isn't a plain string value. Used to show a
// resolved full path in an approval prompt without touching any other part
// of the arguments JSON; see "Full paths in the approval prompt" in
// ARCHITECTURE.md.
core::String replace_json_string_field_value(const core::String& json, const core::String& field_name, const core::String& new_value)
{
	const core::String needle = "\"" + field_name + "\":";
	const size_t field_index = json.find(needle);
	if (field_index == core::String::npos)
	{
		return json;
	}

	size_t cursor = field_index + needle.size();
	while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t'))
	{
		++cursor;
	}
	if (cursor >= json.size() || json[cursor] != '"')
	{
		return json;
	}

	const size_t value_start = cursor;
	++cursor;
	while (cursor < json.size())
	{
		if (json[cursor] == '\\' && cursor + 1 < json.size())
		{
			cursor += 2;
			continue;
		}
		if (json[cursor] == '"')
		{
			++cursor;
			break;
		}
		++cursor;
	}
	const size_t value_end = cursor;

	return json.substr(0, value_start) + "\"" + net::escape_json_string(new_value) + "\"" + json.substr(value_end);
}

// Whether the agent-turn loop must pause and get the user's explicit
// approval before executing this tool, rather than auto-running it. Only
// side-effecting tools are gated - anything read-only (list_dir, get_cwd,
// get_system_info, which, list_connectors, list_tasks, list_open_windows,
// find_application, connector_status, read_file, stat_path, call_connector,
// read_clipboard) keeps auto-running, since gating those would only make
// the agent slower to answer plain questions for no safety benefit.
bool tool_call_requires_approval(const core::String& tool_name)
{
	return tool_name == "run_command"
		|| tool_name == "run_ffmpeg"
		|| tool_name == "write_file"
		|| tool_name == "open_application"
		|| tool_name == "launch_connector"
		|| tool_name == "stop_connector"
		|| tool_name == "enqueue_task"
		|| tool_name == "cancel_task"
		|| tool_name == "copy_file"
		|| tool_name == "move_path"
		|| tool_name == "delete_file"
		|| tool_name == "create_directory"
		|| tool_name == "write_clipboard";
}

// Returns the last up to `max_lines` non-empty, \r-trimmed lines of `text`,
// joined by " | " - used to surface the actual diagnostic buried at the end
// of a verbose command's output. ffmpeg is the main offender: its real error
// always lands in the last line or two, after a wall of build-config and
// stream-probe banner noise the model has no reason to read through to find
// it (a real case: "Invalid size 'h'" - the actual problem - was buried
// under ~2KB of ffmpeg's own version/config preamble).
core::String last_nonempty_lines(const core::String& text, const size_t max_lines)
{
	core::Array<core::String> lines;
	size_t start = 0;
	while (start <= text.size())
	{
		const size_t newline = text.find('\n', start);
		core::String line = text.substr(start, newline == core::String::npos ? core::String::npos : newline - start);
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
		{
			line.pop_back();
		}
		if (!line.empty())
		{
			lines.push_back(line);
		}
		if (newline == core::String::npos)
		{
			break;
		}
		start = newline + 1;
	}

	const size_t take = lines.size() < max_lines ? lines.size() : max_lines;
	core::String joined;
	for (size_t index = lines.size() - take; index < lines.size(); ++index)
	{
		if (!joined.empty())
		{
			joined += " | ";
		}
		joined += lines[index];
	}
	return joined;
}

// A short, unambiguous explanation of why a command_succeeded()==false
// CommandRunResult failed - prefers error_message (spawn failure, timeout),
// then stderr's tail (where a well-behaved program puts errors), then
// stdout's tail (ffmpeg, notably, writes everything - including its actual
// error - there in this codebase's capture).
core::String summarize_command_failure(const CommandRunResult& result)
{
	if (!result.error_message.empty())
	{
		return result.error_message;
	}
	const core::String stderr_tail = last_nonempty_lines(result.stderr_text, 3);
	if (!stderr_tail.empty())
	{
		return stderr_tail;
	}
	return last_nonempty_lines(result.stdout_text, 3);
}

const char* const kUnverifiedActionClaimNudge =
	"Your last response claimed an action was done or about to be done, but no tool call backs "
	"that up anywhere in this conversation turn - nothing has actually happened. If you intend to "
	"do something, call the matching tool right now instead of describing it or claiming it's "
	"already finished. Only report an action as successful when you can point to a tool result "
	"whose \"ok\" field is true - if \"ok\" is false or you never called the tool, say so honestly "
	"and explain the actual problem instead of asserting success. If you're missing information "
	"needed to act, ask a clarifying question instead of claiming you're already doing it.";

const char* const kCapabilityDenialNudge =
	"That's false - you DO have the ability to execute commands directly on this machine: "
	"run_command, run_ffmpeg, write_file, open_application, and the filesystem tools all actually "
	"run when you call them, they are not just descriptions for the user to copy and run themselves. "
	"Call the right tool now instead of telling the user to run something themselves.";

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

		// Hardware inventory only runs if the human opted in at startup
		// (--enable-hardware-scan) - see HostConfig::enable_hardware_scan.
		// Left default-constructed (empty) otherwise; build_hardware_info_json
		// reports that honestly rather than presenting zeroed fields as data.
		if (config_.enable_hardware_scan)
		{
			hardware_info_ = scan_hardware_info();
		}

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
			const core::Array<core::String>& headers,
			int32_t& status_code_out,
			core::String& response_body_out)
		{
			return tools::sync_http_post_json(url, body, status_code_out, response_body_out, headers);
		};

		if (config_.enable_ai)
		{
			language_ai_.set_runtime_enabled(true);
			ai::OllamaConfig ollama_config;
			ollama_config.base_url = config_.ollama_url;
			ollama_config.model = config_.ollama_model;
			ollama_config.enabled = true;
			ollama_config.num_ctx = config_.ollama_num_ctx;
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
					+ (system_info_.desktop_path.empty()
						? core::String()
						: ", the user's Desktop folder is at " + system_info_.desktop_path
							+ " - use this exact path when a request mentions \"the Desktop\", "
							"never guess a \"C:\\Users\\<name>\\Desktop\"-style path yourself")
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
		"droidcli host initialized (" + std::to_string(installed_apps_.size()) + " installed apps indexed"
			+ (config_.enable_hardware_scan ? ", hardware scan enabled" : ", hardware scan disabled") + ")",
		true);
}


void DroidHost::tick(const float delta_seconds)
{
	(void)delta_seconds;
	tick_watchdog();
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

core::String DroidHost::active_model_name() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return (config_.ai_provider == "anthropic") ? config_.anthropic_model : config_.ollama_model;
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
		ollama_config.num_ctx = config_.ollama_num_ctx;
		language_ai_.set_ollama_config(ollama_config);
	}
	persist_current_settings_locked();

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
	const core::String& session_id,
	const core::String& extra_json_fields)
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
		if (!extra_json_fields.empty())
		{
			json_line += "," + extra_json_fields;
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

void DroidHost::tick_watchdog()
{
	if (!config_.enable_ai)
	{
		// --no-ai means there's nothing to watch - don't manufacture
		// "Ollama unreachable" noise for a backend the operator deliberately
		// turned off.
		return;
	}

	const std::time_t now_utc = std::time(nullptr);
	std::time_t last_check = watchdog_last_check_utc_;
	if (!should_emit_periodic_log(now_utc, last_check, kWatchdogIntervalSeconds))
	{
		return;
	}
	watchdog_last_check_utc_ = last_check;

	core::String ollama_url_copy;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ollama_url_copy = config_.ollama_url;
	}

	const core::String tags_url = strip_trailing_slashes(ollama_url_copy) + "/api/tags";
	int32_t status_code = 0;
	core::String response_body;
	const bool transport_ok = tools::sync_http_get(tags_url, status_code, response_body);
	const bool reachable = transport_ok && status_code >= 200 && status_code < 300;

	using namespace std::chrono;
	const int64_t checked_at_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

	bool was_reachable = true;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		was_reachable = ollama_reachable_;
		ollama_reachable_ = reachable;
		ollama_last_check_ms_ = checked_at_ms;
		ollama_last_check_error_ = reachable ? core::String {} : ("status_code=" + std::to_string(status_code));
	}

	// Only log a transition, not every check - this runs at most every
	// kWatchdogIntervalSeconds, but a long-running daemon would still fill
	// the log with "still unreachable" repeats otherwise.
	if (reachable != was_reachable)
	{
		append_app_log(
			"watchdog",
			"ollama",
			reachable
				? "Ollama is reachable again at " + ollama_url_copy
				: "Ollama became unreachable at " + ollama_url_copy + " - agent_turn will keep responding with ok:false rather than hanging or crashing",
			reachable);
	}
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
		ollama_config.num_ctx = config_.ollama_num_ctx;
		language_ai_.set_ollama_config(ollama_config);
	}
	persist_current_settings_locked();

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
	const bool ok = command_succeeded(result);
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
	const bool ok = command_succeeded(result);
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

	// "ok", not "success" - the same gap as open_application/enqueue_task
	// before their fixes (see "Phase 15" in ARCHITECTURE.md): this is also
	// the launch_connector agent tool's result, and "success" is exactly
	// the kind of field name the fabrication guard's success scan does not
	// look for.
	return "{" + net::json_bool_field("ok", ok) + ","
		+ net::json_string_field("error", error) + "}";
}

core::String DroidHost::stop_connector(const core::String& connector_id)
{
	core::String error;
	const bool ok = process_manager_.stop(connector_id, error);
	append_app_log("process", "out",
		ok ? ("stopped connector: " + connector_id) : ("stop failed: " + error), ok);
	return "{" + net::json_bool_field("ok", ok) + ","
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
		// "ok" first, per AGENTS.md's hard rule that every agent-tool result
		// must carry it - this route is also the enqueue_task tool, and a
		// plain "success" field here (the pre-existing shape) is exactly the
		// kind of field the model is not conditioned to trust, per "Phase 6"
		// in ARCHITECTURE.md.
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", error) + "}";
	}

	std::lock_guard<std::mutex> lock(mutex_);
	const core::String id = tasks_.enqueue(task);
	return "{" + net::json_bool_field("ok", true) + ","
		+ net::json_string_field("id", id) + ","
		+ "\"scheduled_for_ms\":" + std::to_string(task.scheduled_for_ms) + "}";
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

core::String DroidHost::cancel_task_json(const core::String& task_id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!tasks_.find(task_id).has_value())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "unknown task") + "}";
	}
	const bool cancelled = tasks_.cancel(task_id);
	if (!cancelled)
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "task is already in a terminal state") + "}";
	}
	return "{" + net::json_bool_field("ok", true) + "}";
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

	{
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

	// append_app_log takes mutex_ itself, so it must run after the block
	// above releases it - this is what makes a scheduled/queued task's
	// dispatch visible in GET /api/app/log and the TUI's log panel, not just
	// GET /api/tasks/{id}'s terminal status.
	append_app_log("task", task.command, task.id + (success ? " completed" : (" failed: " + error)), success);
}

core::String DroidHost::build_self_status_json() const
{
	std::lock_guard<std::mutex> lock(mutex_);

	const core::Array<net::Connector> connector_list = connectors_.list_connectors();
	size_t connectors_enabled = 0;
	for (const net::Connector& connector : connector_list)
	{
		if (connector.enabled)
		{
			++connectors_enabled;
		}
	}

	const core::Array<app::Task> task_list = tasks_.list();
	size_t tasks_pending = 0;
	size_t tasks_running = 0;
	size_t tasks_failed = 0;
	for (const app::Task& task : task_list)
	{
		if (task.status == "pending") { ++tasks_pending; }
		else if (task.status == "running") { ++tasks_running; }
		else if (task.status == "failed") { ++tasks_failed; }
	}

	size_t recent_failures = 0;
	for (size_t index = app_log_.size(); index > 0 && app_log_.size() - index < 20; --index)
	{
		if (!app_log_[index - 1].success)
		{
			++recent_failures;
		}
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", true) << ',';
	stream << net::json_bool_field("ai_enabled", config_.enable_ai) << ',';
	stream << net::json_bool_field("ollama_reachable", ollama_reachable_) << ',';
	stream << "\"ollama_last_check_ms\":" << ollama_last_check_ms_ << ',';
	stream << net::json_string_field("ollama_last_check_error", ollama_last_check_error_) << ',';
	stream << "\"connector_count\":" << connector_list.size() << ',';
	stream << "\"connectors_enabled\":" << connectors_enabled << ',';
	stream << "\"tasks_pending\":" << tasks_pending << ',';
	stream << "\"tasks_running\":" << tasks_running << ',';
	stream << "\"tasks_failed\":" << tasks_failed << ',';
	stream << net::json_bool_field("memory_store_open", memory_store_.is_open()) << ',';
	stream << "\"recent_failures_last_20_log_entries\":" << recent_failures;
	stream << '}';
	return stream.str();
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
	const core::String requested_command = net::extract_json_string_field(body, "command");
	const core::String work_dir = net::extract_json_string_field(body, "work_dir");
	int32_t timeout_ms = 30000;
	extract_json_int_field(body, "timeout_ms", timeout_ms);

	if (requested_command.empty())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_bool_field("launched", false) + ","
			+ "\"exit_code\":0,"
			+ net::json_string_field("stdout", "") + ","
			+ net::json_string_field("stderr", "") + ","
			+ net::json_string_field("error", "missing command") + "}";
	}

	const core::String command = substitute_bare_desktop_token(requested_command, system_info_.desktop_path);
	// Defaults to the real Desktop, not droidcli's own launch directory, when
	// the caller doesn't specify one - see default_bare_filename_to_desktop's
	// comment above for why: a command that writes a bare relative filename
	// (redirection, a tool's own default output) should land somewhere a
	// human would go looking for it, not wherever droidcli happened to start
	// from. Pass work_dir explicitly to run in some other directory instead.
	const core::String effective_work_dir = work_dir.empty() ? system_info_.desktop_path : work_dir;
	const CommandRunResult result = run_command_once(command, effective_work_dir, timeout_ms);
	const bool ok = command_succeeded(result);
	// Previously logged on error_message.empty() alone, which missed a
	// nonzero exit code (error_message is only set for a spawn failure or
	// timeout, not a normal-but-failing run) - the exact same gap that let
	// a failed ffmpeg invocation get reported to the user as a success.
	append_app_log("run", "out", command, ok);

	std::ostringstream stream;
	stream << '{';
	// "ok" first and always present - every other agent tool already
	// returns this as its primary success signal (list_dir, write_file,
	// stat_path, ...); run_command/run_ffmpeg were the two exceptions,
	// leaving the model to infer success from "launched":true (which only
	// means the process started, not that it finished successfully) and a
	// numeric exit_code easy to miss under a large stdout dump. This is
	// what let a failed ffmpeg run (exit_code 22, a filter-graph syntax
	// error) get reported to the user as "successfully created."
	stream << net::json_bool_field("ok", ok) << ',';
	stream << net::json_bool_field("launched", result.launched) << ',';
	stream << "\"exit_code\":" << result.exit_code << ',';
	stream << net::json_string_field("stdout", result.stdout_text) << ',';
	stream << net::json_string_field("stderr", result.stderr_text) << ',';
	stream << net::json_string_field("error", result.error_message);
	if (command != requested_command)
	{
		// The command you (the model) requested referenced a bare "desktop"
		// path that can never resolve relative to droidcli's own working
		// directory - substituted the real Desktop path automatically.
		// Report the actual location back to the user from this field, not
		// from what you originally asked for.
		stream << ',' << net::json_string_field("resolved_command", command);
	}
	if (work_dir.empty())
	{
		// No work_dir was given - report where this actually ran (the real
		// Desktop, see effective_work_dir above), not silently leave the
		// model assuming its own working directory.
		stream << ',' << net::json_string_field("resolved_work_dir", effective_work_dir);
	}
	if (!ok)
	{
		// Spelled out in plain language, not just a raw exit code, since
		// the model has already shown it won't reliably notice a nonzero
		// exit_code buried in a large JSON blob on its own.
		stream << ',' << net::json_string_field("failure_reason",
			"Command failed (exit_code=" + std::to_string(result.exit_code) + "): "
			+ summarize_command_failure(result));
	}
	stream << '}';
	return stream.str();
}

core::String DroidHost::run_ffmpeg_json(const core::String& body)
{
	const core::String requested_args = net::extract_json_string_field(body, "args");
	const core::String work_dir = net::extract_json_string_field(body, "work_dir");
	int32_t timeout_ms = 120000;
	extract_json_int_field(body, "timeout_ms", timeout_ms);

	if (requested_args.empty())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_bool_field("launched", false) + ","
			+ "\"exit_code\":0,"
			+ net::json_string_field("stdout", "") + ","
			+ net::json_string_field("stderr", "") + ","
			+ net::json_string_field("error", "missing args") + "}";
	}

	const core::String args = substitute_bare_desktop_token(requested_args, system_info_.desktop_path);
	// Same default as run_command above: a bare relative output filename in
	// ffmpeg's own args (its most common shape - see "Phase 20" in
	// ARCHITECTURE.md) lands on the real Desktop by default, not wherever
	// droidcli was launched from, unless work_dir says otherwise.
	const core::String effective_work_dir = work_dir.empty() ? system_info_.desktop_path : work_dir;
	const CommandRunResult result = run_ffmpeg(args, effective_work_dir, timeout_ms);
	const bool ok = command_succeeded(result);
	append_app_log("ffmpeg", "out", args, ok);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", ok) << ',';
	stream << net::json_bool_field("launched", result.launched) << ',';
	stream << "\"exit_code\":" << result.exit_code << ',';
	stream << net::json_string_field("stdout", result.stdout_text) << ',';
	stream << net::json_string_field("stderr", result.stderr_text) << ',';
	stream << net::json_string_field("error", result.error_message);
	if (args != requested_args)
	{
		// The args you (the model) requested referenced a bare "desktop"
		// path that can never resolve relative to droidcli's own working
		// directory - substituted the real Desktop path automatically.
		// Report the actual location back to the user from this field, not
		// from what you originally asked for.
		stream << ',' << net::json_string_field("resolved_args", args);
	}
	if (work_dir.empty())
	{
		stream << ',' << net::json_string_field("resolved_work_dir", effective_work_dir);
	}
	if (!ok)
	{
		stream << ',' << net::json_string_field("failure_reason",
			"ffmpeg failed (exit_code=" + std::to_string(result.exit_code) + "): "
			+ summarize_command_failure(result));
	}
	stream << '}';
	return stream.str();
}

DroidHost::ResolvedLaunchTarget DroidHost::resolve_open_application_target(const core::String& path_or_name, const core::String& args) const
{
	// The Windows execution ruleset (see "Windows execution ruleset" in
	// ARCHITECTURE.md): resolve a bare name against every source droidcli
	// actually trusts, in trust order, BEFORE ever letting CreateProcess's
	// own blind bare-name search (calling process's directory, cwd, system
	// directories, PATH) get a chance to run. This was a real, confirmed
	// incident: launch_application's OWN internal blind-search fallback
	// used to be tried FIRST, so if it coincidentally matched *anything* on
	// the search path, "success" was reported before the installed-apps
	// index or droidcli's own curated Windows-locations table - both more
	// specific, more trustworthy matches - ever got a chance to run. A
	// request for "the Windows panel that shows memory usage" reported
	// launching "Memory" successfully with no way to confirm what had
	// actually opened.
	//
	// Shared by open_application() (resolves at execution time) and
	// precheck_and_resolve_gated_call() (resolves BEFORE the approval
	// prompt is ever shown, so an approved call is guaranteed to succeed) -
	// one resolution implementation, not two that could drift apart.
	ResolvedLaunchTarget result;
	result.target = path_or_name;
	result.effective_args = args;

	if (looks_like_path(path_or_name))
	{
		// The caller already gave a path (relative or absolute) - honored
		// exactly as given, same "the caller already specified where"
		// discipline as the filesystem tools (Phase 20/40). Not re-resolved
		// against any of the tiers below - those are only for bare names.
		result.resolved = true;
		result.source = "given_path";
		return result;
	}

	const core::String registry_path = resolve_app_paths_registry(path_or_name);
	if (!registry_path.empty())
	{
		result.resolved = true;
		result.target = registry_path;
		result.source = "app_paths_registry";
		return result;
	}

	core::String indexed_path;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		indexed_path = find_installed_app_match(installed_apps_, path_or_name);
	}
	if (!indexed_path.empty())
	{
		result.resolved = true;
		result.target = indexed_path;
		result.source = "installed_apps_index";
		return result;
	}

	WellKnownWindowsTarget windows_target;
	if (find_well_known_windows_target(path_or_name, windows_target))
	{
		// Resolved further to a full absolute path (rather than left as
		// e.g. "taskmgr.exe") so every successful resolution tier ends in a
		// real, verified full path, not just the curated ones - "always
		// launch with a full path", not merely "always launch something
		// real".
		const WhichResult which_result = which_executable(windows_target.path_or_name);
		result.resolved = true;
		result.target = which_result.ok ? which_result.resolved_path : windows_target.path_or_name;
		result.source = "windows_known_location";
		if (result.effective_args.empty())
		{
			result.effective_args = windows_target.args;
		}
		return result;
	}

	// Nothing curated matched. Last resort: a *verified* PATH search
	// (which_executable checks the candidate file actually exists before
	// returning it) rather than a blind CreateProcess bare-name call that
	// offers no such check - strictly more verification than the old
	// behavior, though still the lowest-trust tier, since anything
	// coincidentally on PATH can still match.
	const WhichResult which_result = which_executable(path_or_name);
	if (which_result.ok)
	{
		result.resolved = true;
		result.target = which_result.resolved_path;
		result.source = "path_search";
		return result;
	}

	// Nothing - not App Paths, not the installed-apps index, not droidcli's
	// own knowledge of Windows, not even a raw PATH search - resolved this
	// to anything real.
	result.error_message = "'" + path_or_name + "' could not be resolved to a real, existing "
		"executable via the Windows App Paths registry, the installed-apps index, droidcli's "
		"built-in knowledge of Windows locations (call list_windows_locations to see what it "
		"knows), or a PATH search. Ask the user for the exact executable name or full path "
		"rather than guessing.";
	return result;
}

bool DroidHost::precheck_and_resolve_gated_call(ai::ToolCall& call, core::String& out_result_json) const
{
	if (call.name != "open_application")
	{
		return true;
	}

	const core::String path_or_name = net::extract_json_string_field(call.arguments_json, "path_or_name");
	const core::String args = net::extract_json_string_field(call.arguments_json, "args");
	const core::String work_dir = net::extract_json_string_field(call.arguments_json, "work_dir");

	if (path_or_name.empty())
	{
		// Let the normal execution path produce its existing
		// "missing path_or_name" error - not this method's concern.
		return true;
	}

	const ResolvedLaunchTarget resolved = resolve_open_application_target(path_or_name, args);
	if (!resolved.resolved)
	{
		// Never propose a yes/no for something guaranteed to fail (see
		// "Never propose an unresolvable action" in ARCHITECTURE.md) - build
		// the exact same failure shape open_application() itself would
		// return, so the fabrication guard/retry-nudge machinery downstream
		// treats it identically to a real, executed failure.
		out_result_json = "{" + net::json_bool_field("ok", false) + ","
			+ net::json_bool_field("launched", false) + ","
			+ "\"pid\":0,"
			+ net::json_string_field("resolved_path", "") + ","
			+ net::json_string_field("resolution_source", "") + ","
			+ net::json_string_field("error", resolved.error_message)
			+ "}";
		return false;
	}

	// Rewrite in place - what the human approves and what actually executes
	// are now guaranteed to be the exact same, already-verified target.
	call.arguments_json = "{"
		+ net::json_string_field("path_or_name", resolved.target) + ","
		+ net::json_string_field("args", resolved.effective_args) + ","
		+ net::json_string_field("work_dir", work_dir)
		+ "}";
	return true;
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

	const ResolvedLaunchTarget resolved = resolve_open_application_target(path_or_name, args);
	const core::String resolution_source = resolved.source;

	LaunchAppResult result;
	if (!resolved.resolved)
	{
		result.error_message = resolved.error_message;
	}
	else
	{
		result = launch_application(resolved.target, resolved.effective_args, work_dir);
	}

	append_app_log("open", "out", path_or_name, result.launched);

	std::ostringstream stream;
	stream << '{';
	// "ok" first, mirroring "launched" - this was the exact field missing
	// that caused a real, confirmed incident: open_application succeeded
	// (Notepad genuinely launched, PID logged) but the fabrication guard's
	// a_tool_call_already_succeeded_this_turn scan (cli/host.cpp) found no
	// "ok" field to read, concluded nothing had succeeded, and the loop
	// went on to tell the user "I wasn't actually able to complete this" -
	// a lie about a real success. See "Phase 15" in ARCHITECTURE.md.
	stream << net::json_bool_field("ok", result.launched) << ',';
	stream << net::json_bool_field("launched", result.launched) << ',';
	stream << "\"pid\":" << result.pid << ',';
	// The real path the OS actually launched (queried back from the live
	// process, cli/command_runner.cpp), not just an echo of path_or_name -
	// see "Windows app execution transparency" in ARCHITECTURE.md. Empty
	// when unavailable (a failed launch, or the query itself failing).
	stream << net::json_string_field("resolved_path", result.resolved_path) << ',';
	// Which trust tier actually matched path_or_name - "given_path",
	// "app_paths_registry", "installed_apps_index", "windows_known_location",
	// "path_search", or empty on outright failure. See "Windows execution
	// ruleset" in ARCHITECTURE.md.
	stream << net::json_string_field("resolution_source", resolution_source) << ',';
	stream << net::json_string_field("error", result.error_message);
	stream << '}';
	return stream.str();
}

core::String DroidHost::find_applications_json(const core::String& body) const
{
	const core::String query = net::extract_json_string_field(body, "query");

	std::lock_guard<std::mutex> lock(mutex_);
	std::ostringstream stream;
	stream << '{' << net::json_bool_field("ok", true) << ",\"matches\":[";
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
	stream << ']';

	// Only checked when the installed-apps index came up empty - a real
	// third-party installed app of the same name (found above) always takes
	// priority over droidcli's own built-in Windows knowledge.
	if (candidates.empty())
	{
		WellKnownWindowsTarget windows_target;
		if (find_well_known_windows_target(parsed.app_name, windows_target))
		{
			stream << ',' << net::json_bool_field("resolved_windows_target", true);
			stream << ',' << net::json_string_field("windows_target_display_name", windows_target.display_name);
			stream << ',' << net::json_string_field("windows_target_path_or_name", windows_target.path_or_name);
			stream << ',' << net::json_string_field("windows_target_args", windows_target.args);
		}
	}
	stream << '}';
	return stream.str();
}

core::String DroidHost::list_open_windows_json() const
{
	const core::Array<OpenWindowInfo> windows = cli::list_open_windows();

	std::ostringstream stream;
	stream << '{' << net::json_bool_field("ok", true) << ",\"windows\":[";
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

	// Same guard/resolution trio every other filesystem tool already has -
	// read_file was left out of it, and a real transcript showed the model's
	// literal-placeholder guess ("/Users/username/Desktop/folder_name")
	// reach the OS untouched instead of being rejected up front with
	// guidance toward list_dir/get_system_info.
	if (looks_like_placeholder_path(path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("content", "") + ","
			+ "\"size_bytes\":0,"
			+ net::json_bool_field("truncated", false) + ","
			+ net::json_string_field("error",
				"this looks like a placeholder path (e.g. \"/Users/username/...\"), not a real one - "
				"call list_dir on the actual directory (or get_system_info for the real username/"
				"Desktop path) first, then retry with that exact path") + "}";
	}
	const core::String resolved_path = default_bare_filename_to_desktop(
		substitute_bare_desktop_token(path, system_info_.desktop_path), system_info_.desktop_path);
	if (looks_like_invented_desktop_path(resolved_path, system_info_.desktop_path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("content", "") + ","
			+ "\"size_bytes\":0,"
			+ net::json_bool_field("truncated", false) + ","
			+ net::json_string_field("error",
				"this path invents its own \"desktop\" location instead of using the real one - "
				"the actual Desktop is " + system_info_.desktop_path + ", use that exact path") + "}";
	}

	const FileReadResult result = cli::read_file(resolved_path, max_bytes);
	append_app_log("fs", "in", "read " + resolved_path, result.ok);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", result.ok) << ',';
	stream << net::json_string_field("content", result.content) << ',';
	stream << "\"size_bytes\":" << result.size_bytes << ',';
	stream << net::json_bool_field("truncated", result.truncated) << ',';
	stream << net::json_string_field("error", result.error_message);
	if (resolved_path != path)
	{
		stream << ',' << net::json_string_field("resolved_path", resolved_path);
	}
	stream << '}';
	return stream.str();
}

core::String DroidHost::write_file(const core::String& body)
{
	const core::String path = net::extract_json_string_field(body, "path");
	const core::String content = net::extract_json_string_field(body, "content");
	bool append_mode = false;
	net::extract_json_bool_field(body, "append", append_mode);

	// Same guard/resolution pair copy_file/move_path/delete_file already got
	// in Phase 16 - write_file was left out of that pass, and a real
	// transcript showed exactly the gap that leaves: a bare "/Desktop/..."
	// path isn't resolved against the real Desktop, and on Windows a leading
	// "/" means "root of the current drive," not "root of the filesystem" -
	// std::filesystem silently wrote (and auto-created the parent for) a
	// bogus "C:\Desktop\..." that has nothing to do with the user's actual
	// Desktop, returning a perfectly genuine ok:true the whole time.
	if (looks_like_placeholder_path(path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"this looks like a placeholder path (e.g. \"/path/to/...\"), not a real one - "
				"call list_dir or stat_path on the actual directory first to find the real location, "
				"then retry with that exact path") + "}";
	}
	const core::String resolved_path = default_bare_filename_to_desktop(
		substitute_bare_desktop_token(path, system_info_.desktop_path), system_info_.desktop_path);
	if (looks_like_invented_desktop_path(resolved_path, system_info_.desktop_path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"this path invents its own \"desktop\" location instead of using the real one - "
				"the actual Desktop is " + system_info_.desktop_path + ", use that exact path") + "}";
	}

	if (looks_like_mismatched_binary_content(resolved_path, content))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"the path looks like a real image/media file, but the content does not start with "
				"that format's actual file signature - write_file writes exact text, it cannot "
				"generate real binary image/media data. Use run_ffmpeg to actually create an image "
				"or media file, then write_file for text files only") + "}";
	}

	const FileWriteResult result = cli::write_file(resolved_path, content, append_mode);
	append_app_log("fs", "out", (append_mode ? "append " : "write ") + resolved_path, result.ok);

	// Ground truth, not self-report: cli::write_file() reporting bytes_written
	// only means the ofstream accepted that many bytes - it says nothing about
	// whether the path it wrote to is actually the one the user meant. A fresh
	// stat_path() call here inspects the real filesystem state after the write
	// completes, the same way a human would run `ls` to confirm a command
	// actually did what it claimed instead of trusting the command's own exit
	// code. See "Phase 18" in ARCHITECTURE.md for the transcript that showed
	// why the write's own report isn't enough on its own.
	StatResult verification;
	if (result.ok)
	{
		verification = cli::stat_path(resolved_path);
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", result.ok) << ',';
	stream << "\"bytes_written\":" << result.bytes_written << ',';
	stream << net::json_string_field("error", result.error_message);
	if (resolved_path != path)
	{
		stream << ',' << net::json_string_field("resolved_path", resolved_path);
	}
	if (result.ok)
	{
		stream << ',' << net::json_bool_field("verified_exists", verification.ok && verification.exists && verification.is_file);
		stream << ',' << "\"verified_size_bytes\":" << verification.size_bytes;
	}
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
	return "{" + net::json_bool_field("ok", true) + ","
		+ net::json_string_field("cwd", get_current_working_directory()) + "}";
}

core::String DroidHost::build_system_info_json() const
{
	// current_datetime is computed fresh on every call, unlike the rest of
	// system_info_ (queried once at startup and cached) - "what time is it"
	// goes stale the instant it's cached, and a real gap showed the model has
	// no other way to know the current date/time at all: it's never in the
	// seeded system prompt (only sent once, at session start - see
	// agent_turn()) and no tool ever reported it, so a long-running session
	// had no way to reason about elapsed time or the actual current date.
	return "{"
		+ net::json_bool_field("ok", true) + ","
		+ net::json_string_field("os_name", system_info_.os_name) + ","
		+ net::json_string_field("os_version", system_info_.os_version) + ","
		+ net::json_string_field("architecture", system_info_.architecture) + ","
		+ net::json_string_field("hostname", system_info_.hostname) + ","
		+ net::json_string_field("username", system_info_.username) + ","
		+ net::json_string_field("cwd", system_info_.cwd) + ","
		+ net::json_string_field("desktop_path", system_info_.desktop_path) + ","
		+ net::json_string_field("current_datetime", make_full_log_timestamp())
		+ "}";
}

core::String DroidHost::build_hardware_info_json() const
{
	if (!config_.enable_hardware_scan)
	{
		return "{" + net::json_bool_field("ok", true) + ","
			+ net::json_bool_field("enabled", false) + ","
			+ net::json_string_field("error", "hardware scanning is disabled - restart droidcli with --enable-hardware-scan to turn it on") + "}";
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", true) << ',';
	stream << net::json_bool_field("enabled", true) << ',';
	stream << net::json_string_field("cpu_name", hardware_info_.cpu_name) << ',';
	stream << "\"cpu_core_count\":" << hardware_info_.cpu_core_count << ',';
	stream << "\"total_ram_bytes\":" << hardware_info_.total_ram_bytes << ',';
	stream << "\"gpus\":[";
	for (size_t index = 0; index < hardware_info_.gpus.size(); ++index)
	{
		if (index > 0) { stream << ','; }
		stream << '{' << net::json_string_field("name", hardware_info_.gpus[index].name) << '}';
	}
	stream << "],";
	stream << "\"disks\":[";
	for (size_t index = 0; index < hardware_info_.disks.size(); ++index)
	{
		if (index > 0) { stream << ','; }
		const DiskVolume& disk = hardware_info_.disks[index];
		stream << '{'
			<< net::json_string_field("drive", disk.drive) << ','
			<< "\"total_bytes\":" << disk.total_bytes << ','
			<< "\"free_bytes\":" << disk.free_bytes
			<< '}';
	}
	stream << "]}";
	return stream.str();
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

core::String DroidHost::copy_file_json(const core::String& body)
{
	const core::String source_path = net::extract_json_string_field(body, "source_path");
	const core::String destination_path = net::extract_json_string_field(body, "destination_path");

	if (looks_like_placeholder_path(source_path) || looks_like_placeholder_path(destination_path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"this looks like a placeholder path (e.g. \"/path/to/...\"), not a real one - "
				"call list_dir or stat_path on the actual directory first to find the real file, "
				"then retry with that exact path") + "}";
	}

	const core::String resolved_source = default_bare_filename_to_desktop(
		substitute_bare_desktop_token(source_path, system_info_.desktop_path), system_info_.desktop_path);
	const core::String resolved_destination = default_bare_filename_to_desktop(
		substitute_bare_desktop_token(destination_path, system_info_.desktop_path), system_info_.desktop_path);
	if (looks_like_invented_desktop_path(resolved_source, system_info_.desktop_path)
		|| looks_like_invented_desktop_path(resolved_destination, system_info_.desktop_path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"one of these paths invents its own \"desktop\" location instead of using the real one - "
				"the actual Desktop is " + system_info_.desktop_path + ", use that exact path") + "}";
	}
	const FileOpResult result = cli::copy_file(resolved_source, resolved_destination);
	append_app_log("fs", "out", "copy " + resolved_source + " -> " + resolved_destination, result.ok);

	// Same immediate, independent ground-truth check as write_file/
	// create_directory (Phase 18/23) - a fresh stat_path() on the
	// destination right after the copy, not a repeat of what copy_file's
	// own return value already claims.
	StatResult verification;
	if (result.ok)
	{
		verification = cli::stat_path(resolved_destination);
	}

	core::String response = "{" + net::json_bool_field("ok", result.ok) + ","
		+ net::json_string_field("error", result.error_message);
	if (resolved_source != source_path || resolved_destination != destination_path)
	{
		response += "," + net::json_string_field("resolved_source_path", resolved_source)
			+ "," + net::json_string_field("resolved_destination_path", resolved_destination);
	}
	if (result.ok)
	{
		response += "," + net::json_bool_field("verified_exists", verification.ok && verification.exists && verification.is_file)
			+ "," + ("\"verified_size_bytes\":" + std::to_string(verification.size_bytes));
	}
	return response + "}";
}

core::String DroidHost::move_path_json(const core::String& body)
{
	const core::String source_path = net::extract_json_string_field(body, "source_path");
	const core::String destination_path = net::extract_json_string_field(body, "destination_path");

	if (looks_like_placeholder_path(source_path) || looks_like_placeholder_path(destination_path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"this looks like a placeholder path (e.g. \"/path/to/...\"), not a real one - "
				"call list_dir or stat_path on the actual directory first to find the real file, "
				"then retry with that exact path") + "}";
	}

	const core::String resolved_source = default_bare_filename_to_desktop(
		substitute_bare_desktop_token(source_path, system_info_.desktop_path), system_info_.desktop_path);
	const core::String resolved_destination = default_bare_filename_to_desktop(
		substitute_bare_desktop_token(destination_path, system_info_.desktop_path), system_info_.desktop_path);
	if (looks_like_invented_desktop_path(resolved_source, system_info_.desktop_path)
		|| looks_like_invented_desktop_path(resolved_destination, system_info_.desktop_path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"one of these paths invents its own \"desktop\" location instead of using the real one - "
				"the actual Desktop is " + system_info_.desktop_path + ", use that exact path") + "}";
	}
	const FileOpResult result = cli::move_path(resolved_source, resolved_destination);
	append_app_log("fs", "out", "move " + resolved_source + " -> " + resolved_destination, result.ok);

	// Immediate, independent ground-truth check (Phase 24, same pattern as
	// write_file/create_directory/copy_file): a real move means the
	// destination now exists AND the source is gone - checking only one side
	// would miss a "copied but didn't remove the original" half-failure.
	StatResult destination_check;
	StatResult source_check;
	if (result.ok)
	{
		destination_check = cli::stat_path(resolved_destination);
		source_check = cli::stat_path(resolved_source);
	}

	core::String response = "{" + net::json_bool_field("ok", result.ok) + ","
		+ net::json_string_field("error", result.error_message);
	if (resolved_source != source_path || resolved_destination != destination_path)
	{
		response += "," + net::json_string_field("resolved_source_path", resolved_source)
			+ "," + net::json_string_field("resolved_destination_path", resolved_destination);
	}
	if (result.ok)
	{
		response += "," + net::json_bool_field("verified_exists", destination_check.ok && destination_check.exists)
			+ "," + net::json_bool_field("verified_source_removed", source_check.ok && !source_check.exists);
	}
	return response + "}";
}

core::String DroidHost::delete_file_json(const core::String& body)
{
	const core::String path = net::extract_json_string_field(body, "path");

	if (looks_like_placeholder_path(path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"this looks like a placeholder path (e.g. \"/path/to/...\"), not a real one - "
				"call list_dir or stat_path on the actual directory first to find the real file, "
				"then retry with that exact path") + "}";
	}

	const core::String resolved_path = default_bare_filename_to_desktop(
		substitute_bare_desktop_token(path, system_info_.desktop_path), system_info_.desktop_path);
	if (looks_like_invented_desktop_path(resolved_path, system_info_.desktop_path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"this path invents its own \"desktop\" location instead of using the real one - "
				"the actual Desktop is " + system_info_.desktop_path + ", use that exact path") + "}";
	}
	const FileOpResult result = cli::delete_file(resolved_path);
	append_app_log("fs", "out", "delete " + resolved_path, result.ok);

	// Immediate, independent ground-truth check (Phase 24): a real delete
	// means the path no longer exists - a fresh stat_path() confirms that,
	// rather than trusting delete_file's own return value alone.
	StatResult verification;
	if (result.ok)
	{
		verification = cli::stat_path(resolved_path);
	}

	core::String response = "{" + net::json_bool_field("ok", result.ok) + ","
		+ net::json_string_field("error", result.error_message);
	if (resolved_path != path)
	{
		response += "," + net::json_string_field("resolved_path", resolved_path);
	}
	if (result.ok)
	{
		response += "," + net::json_bool_field("verified_deleted", verification.ok && !verification.exists);
	}
	return response + "}";
}

core::String DroidHost::create_directory_json(const core::String& body)
{
	const core::String path = net::extract_json_string_field(body, "path");

	if (looks_like_placeholder_path(path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"this looks like a placeholder path (e.g. \"/path/to/...\"), not a real one - "
				"call list_dir or stat_path on the actual parent directory first to find the real "
				"location, then retry with that exact path") + "}";
	}

	const core::String resolved_path = default_bare_filename_to_desktop(
		substitute_bare_desktop_token(path, system_info_.desktop_path), system_info_.desktop_path);
	if (looks_like_invented_desktop_path(resolved_path, system_info_.desktop_path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"this path invents its own \"desktop\" location instead of using the real one - "
				"the actual Desktop is " + system_info_.desktop_path + ", use that exact path") + "}";
	}
	const FileOpResult result = cli::create_directory(resolved_path);
	append_app_log("fs", "out", "mkdir " + resolved_path, result.ok);

	// Same ground-truth pattern as write_file (Phase 18): a fresh stat_path()
	// after the fact confirms a real directory actually exists there now,
	// not just that the create call itself claims to have succeeded.
	StatResult verification;
	if (result.ok)
	{
		verification = cli::stat_path(resolved_path);
	}

	core::String response = "{" + net::json_bool_field("ok", result.ok) + ","
		+ net::json_string_field("error", result.error_message);
	if (resolved_path != path)
	{
		response += "," + net::json_string_field("resolved_path", resolved_path);
	}
	if (result.ok)
	{
		response += "," + net::json_bool_field("verified_is_directory", verification.ok && verification.exists && verification.is_dir);
	}
	return response + "}";
}

core::String DroidHost::read_clipboard_json() const
{
	const ClipboardReadResult result = read_text_from_clipboard();
	return "{" + net::json_bool_field("ok", result.ok) + ","
		+ net::json_string_field("text", result.text) + ","
		+ net::json_string_field("error", result.error_message) + "}";
}

core::String DroidHost::write_clipboard_json(const core::String& body)
{
	const core::String text = net::extract_json_string_field(body, "text");
	const ClipboardWriteResult result = write_text_to_clipboard(text);
	append_app_log("clipboard", "out", "wrote " + std::to_string(text.size()) + " chars to clipboard", result.ok);

	// Immediate, independent ground-truth check (Phase 24, same pattern as
	// the filesystem tools): read the clipboard back right after writing it
	// and confirm it actually matches, rather than trusting the write call's
	// own report - a real risk here specifically, since another application
	// (or the user, mid-copy) can win a race for clipboard ownership between
	// this write and whenever the model reports success.
	bool verified_matches = false;
	if (result.ok)
	{
		const ClipboardReadResult verification = read_text_from_clipboard();
		verified_matches = verification.ok && verification.text == text;
	}

	return "{" + net::json_bool_field("ok", result.ok) + ","
		+ net::json_string_field("error", result.error_message) + ","
		+ net::json_bool_field("verified_matches", verified_matches) + "}";
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
		"Queue a task for droidcli's background task loop to process later: \"launch\"/\"stop\" a connector_id, \"run\" a shell command (payload_json {\"command\":..,\"work_dir\":..}), or any other command string treated as an HTTP path called on connector_id. Pass delay_ms to schedule it for later instead of as soon as possible - e.g. delay_ms:120000 for \"do this in 2 minutes\" - the task stays visible as pending (with its due time) until then; omit delay_ms (or pass 0) to make it claimable immediately, same as before. Pass recurrence_ms to make it recurring instead of one-shot - e.g. recurrence_ms:3600000 for \"run this every hour\" - after each run it automatically reschedules itself recurrence_ms later (whether that run succeeded or failed) instead of terminating; call cancel_task to stop it for good. Use delay_ms and recurrence_ms together to also control when the *first* run happens.",
		"{\"type\":\"object\",\"properties\":{"
		"\"connector_id\":{\"type\":\"string\",\"description\":\"optional, required for launch/stop/http-path commands\"},"
		"\"command\":{\"type\":\"string\",\"description\":\"launch | stop | run | <http path>\"},"
		"\"payload_json\":{\"type\":\"string\",\"description\":\"optional raw JSON payload\"},"
		"\"delay_ms\":{\"type\":\"integer\",\"description\":\"optional - milliseconds from now before this task's first run becomes runnable, e.g. 120000 for 'in 2 minutes'\"},"
		"\"recurrence_ms\":{\"type\":\"integer\",\"description\":\"optional - milliseconds between runs, e.g. 3600000 for 'every hour'; omit (or 0) for one-shot\"}"
		"},\"required\":[\"command\"]}"});

	tools.push_back(ai::ToolDefinition{
		"list_tasks",
		"List all tasks in the task queue with their status (pending/running/done/failed/cancelled), recurrence_ms (0 means one-shot), and run_count (how many times it has run so far). Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"cancel_task",
		"Stop a task for good - the only way to end a recurring task (enqueue_task's recurrence_ms), which would otherwise keep rescheduling itself forever. Also works on a one-shot task that's still pending/running (e.g. to cancel a delayed task before it fires). ok:false if the task id is unknown or it's already in a terminal state.",
		"{\"type\":\"object\",\"properties\":{"
		"\"task_id\":{\"type\":\"string\",\"description\":\"the task id, from enqueue_task's result or list_tasks\"}"
		"},\"required\":[\"task_id\"]}"});

	tools.push_back(ai::ToolDefinition{
		"run_command",
		"Run a one-shot shell command synchronously and capture its stdout/stderr/exit code. This executes arbitrary shell commands on the host machine - use only for tasks the user actually asked for. If your command contains a bare relative \"desktop/...\" path, it's automatically replaced with the real Desktop folder (see get_system_info's desktop_path) before running - the result includes a \"resolved_command\" field when that happened, report the location from there, not from what you originally typed. If you don't pass work_dir, this runs in the user's real Desktop folder by default (not droidcli's own working directory) - so a bare relative output filename the command writes lands there, not in droidcli's own directory. The result includes a \"resolved_work_dir\" field whenever that default applied; pass work_dir explicitly to run somewhere else instead.",
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
		"list_windows_locations",
		"List every built-in Windows panel/location/Settings page droidcli knows how to open by name (Task Manager, Resource Monitor, Performance Monitor, Device Manager, Control Panel, Recycle Bin, Windows Security, etc.) - this is droidcli's own hand-maintained knowledge of Windows itself, separate from the installed-apps index (find_application). Call this when the user asks what you can open, what panels/settings are available, or similar - answer from this real list, never guess or invent one. Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"open_application",
		"Open/launch a GUI application (e.g. Notepad, a browser, an image viewer) OR a built-in Windows location/Settings page (e.g. Recycle Bin, Sound Settings, Control Panel, Task Manager, Device Manager, This PC) so the user can see and use it. Detached - does not wait for it to close and does not capture output. Use this instead of run_command for opening apps, since run_command waits for the process to exit and GUI apps don't exit on their own. A bare name is resolved in trust order - the Windows App Paths registry, then the installed-apps index (find_application's data source), then droidcli's own built-in knowledge of Windows itself (list_windows_locations) for things that are not installed applications at all, then, only as a last resort, a verified PATH search - just pass the name naturally ('Recycle Bin', 'Sound Settings'), you do not need to know or construct the underlying ms-settings:/shell: target yourself. For a vague/descriptive request ('the panel that shows memory usage', 'the thing for managing disk partitions') call list_windows_locations first and pass back the real name it returns, rather than guessing a path_or_name yourself. If you're not confident which exact app/path the user means (ambiguous name, multiple plausible matches from find_application, or a prior call failed to resolve it), ask the user to confirm rather than guessing. A successful result's \"resolved_path\" is the real path the OS actually launched (not just an echo of path_or_name), and \"resolution_source\" says which of those tiers matched - report the resolved_path (or its filename) back to the user so they know exactly what opened, especially for a loosely-worded request. An unresolved bare name is rejected outright with a clear error rather than attempted blindly - if you see that error, ask the user for the exact name or full path instead of retrying the same guess.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path_or_name\":{\"type\":\"string\",\"description\":\"executable name (e.g. 'chrome', 'notepad.exe') or a full path\"},"
		"\"args\":{\"type\":\"string\",\"description\":\"optional command-line arguments\"},"
		"\"work_dir\":{\"type\":\"string\",\"description\":\"optional working directory\"}"
		"},\"required\":[\"path_or_name\"]}"});

	tools.push_back(ai::ToolDefinition{
		"read_file",
		"Read a file's contents from the local filesystem. Caps the read at max_bytes (default 65536) to avoid overloading context - the result reports whether the file was truncated. If you are not certain of the exact path, call list_dir on the real directory (or get_system_info for the real username/Desktop path) first - do not guess or invent a path (e.g. a template like \"/Users/username/...\"), that is rejected outright rather than attempted. A bare \"desktop/...\" token or a bare filename with no directory at all is resolved against the real Desktop - report the location from \"resolved_path\" when present.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\",\"description\":\"file path, absolute or relative to droidcli's working directory\"},"
		"\"max_bytes\":{\"type\":\"integer\",\"description\":\"optional read cap, default 65536\"}"
		"},\"required\":[\"path\"]}"});

	tools.push_back(ai::ToolDefinition{
		"write_file",
		"Create a new file, or overwrite/append to an existing one, on the local filesystem. Creates the file if it doesn't exist yet (this is also the tool to use for 'create a file'), and creates any missing parent directories. Overwrites by default - use only for tasks the user actually asked for. If your path contains a bare \"desktop/...\" token, it's automatically replaced with the real Desktop folder (see get_system_info's desktop_path) before writing - the result includes a \"resolved_path\" field when that happened, report the location from there, not from what you originally typed. If path is just a bare filename with no directory in it at all (e.g. 'notes.txt', not './notes.txt' or 'subdir/notes.txt'), it's created on the user's real Desktop by default, not droidcli's own working directory - the result's \"resolved_path\" reports that too. The result also includes \"verified_exists\"/\"verified_size_bytes\", a fresh, independent filesystem check performed right after the write - these are ground truth, not a repeat of what you asked for; trust them over your own assumption about what got written, and never claim success beyond what \"verified_exists\" actually confirms. This does NOT write real binary image/audio/video data for you - content is written exactly as given as text, so it cannot produce a valid image file; use run_ffmpeg to actually generate image/media files.",
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
		"copy_file",
		"Copy a single file from source_path to destination_path on the local filesystem, creating destination parent directories if needed. Overwrites an existing destination file. Only files, not directories. If you are not certain of the exact source filename, call list_dir on its directory first to find the real name - do not guess or invent a path (e.g. a template like \"/path/to/file.png\"), that is rejected outright rather than attempted. A bare \"desktop/...\" token is auto-resolved to the real Desktop folder, and a bare filename with no directory at all in either source_path or destination_path defaults to the real Desktop too, not droidcli's own working directory - report the location from the result's \"resolved_source_path\"/\"resolved_destination_path\" fields when present, not from what you originally typed. The result also includes \"verified_exists\"/\"verified_size_bytes\", a fresh, independent filesystem check on the destination performed right after the copy - ground truth, not a repeat of what you asked for.",
		"{\"type\":\"object\",\"properties\":{"
		"\"source_path\":{\"type\":\"string\",\"description\":\"file to copy - the real path, not a guess\"},"
		"\"destination_path\":{\"type\":\"string\",\"description\":\"where to copy it to\"}"
		"},\"required\":[\"source_path\",\"destination_path\"]}"});

	tools.push_back(ai::ToolDefinition{
		"move_path",
		"Move/rename a file or directory from source_path to destination_path on the local filesystem, creating destination parent directories if needed. Overwrites an existing destination file. If you are not certain of the exact source path, call list_dir/stat_path first to find the real one - a guessed or template path (e.g. \"/path/to/file.png\") is rejected outright rather than attempted. A bare \"desktop/...\" token is auto-resolved to the real Desktop folder, and a bare filename with no directory at all in either source_path or destination_path defaults to the real Desktop too, not droidcli's own working directory - report the location from \"resolved_source_path\"/\"resolved_destination_path\" when present. The result also includes \"verified_exists\" (the destination, checked fresh right after the move) and \"verified_source_removed\" (the source no longer exists) - ground truth, not a repeat of what you asked for; a real move means both are true, not just one.",
		"{\"type\":\"object\",\"properties\":{"
		"\"source_path\":{\"type\":\"string\",\"description\":\"file or directory to move - the real path, not a guess\"},"
		"\"destination_path\":{\"type\":\"string\",\"description\":\"where to move it to\"}"
		"},\"required\":[\"source_path\",\"destination_path\"]}"});

	tools.push_back(ai::ToolDefinition{
		"delete_file",
		"Permanently delete a single file from the local filesystem. Only files, not directories - this never does a recursive directory delete. This is destructive and cannot be undone - only use it for a file the user actually asked to remove, and only a real path confirmed via list_dir/stat_path, never a guess. A bare filename with no directory at all defaults to the real Desktop, same as the other file tools - report the location from \"resolved_path\" when present. The result also includes \"verified_deleted\", a fresh, independent filesystem check performed right after the delete confirming the path no longer exists - ground truth, not a repeat of what you asked for.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\",\"description\":\"file to delete - the real path, not a guess\"}"
		"},\"required\":[\"path\"]}"});

	tools.push_back(ai::ToolDefinition{
		"create_directory",
		"Create a real directory (folder) on the local filesystem, creating any missing parent directories too. This is the tool for 'create a folder'/'make a directory' - write_file creates a FILE, never a real directory, even with empty content. Idempotent: succeeds (ok:true) if the directory already exists. A bare name with no directory at all defaults to the real Desktop, same as the other file tools - report the location from \"resolved_path\" when present. The result also includes \"verified_is_directory\", a fresh, independent filesystem check performed right after creation - ground truth, not a repeat of what you asked for.",
		"{\"type\":\"object\",\"properties\":{"
		"\"path\":{\"type\":\"string\",\"description\":\"the directory to create\"}"
		"},\"required\":[\"path\"]}"});

	tools.push_back(ai::ToolDefinition{
		"read_clipboard",
		"Read the current text content of the OS clipboard (whatever the user last copied, e.g. with Ctrl+C). Returns ok:false if the clipboard doesn't currently hold text (empty, or holds an image/file selection instead). Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"write_clipboard",
		"Replace the OS clipboard's current content with the given text, so the user can paste it (e.g. with Ctrl+V) into any other application. The result includes \"verified_matches\", a fresh read-back of the clipboard performed right after the write confirming it actually holds the exact text you set - ground truth, not a repeat of what you asked for; another application (or the user, mid-copy) can race for clipboard ownership, so trust this field over assuming the write stuck.",
		"{\"type\":\"object\",\"properties\":{"
		"\"text\":{\"type\":\"string\",\"description\":\"text to place on the clipboard\"}"
		"},\"required\":[\"text\"]}"});

	tools.push_back(ai::ToolDefinition{
		"run_ffmpeg",
		"Run the ffmpeg CLI for media transcode/convert/clip/extract/thumbnail work - the binary is resolved automatically (PATH, then $DROIDCLI_FFMPEG_ROOT), you don't need to know where it lives. args is the raw ffmpeg argument string exactly as you'd type it after 'ffmpeg', e.g. '-y -i input.mp4 -vf scale=1280:-1 output.mp4' or '-i in.wav -ar 16000 out.wav'. Always pass -y to overwrite outputs without prompting, since there is no interactive terminal to answer that prompt. To generate a single static image from nothing (a solid color, a test pattern, etc.) rather than transcode an existing file, use the lavfi virtual input with -frames:v 1 and -update 1 - both are required or the image2 muxer rejects a plain (non-%d-pattern) output filename with 'does not contain an image sequence pattern': '-y -f lavfi -i color=red:s=512x512 -frames:v 1 -update 1 output.png'. If your args contain a bare relative \"desktop/...\" path, it's automatically replaced with the real Desktop folder (see get_system_info's desktop_path) before running - the result includes a \"resolved_args\" field when that happened, report the location from there, not from what you originally typed. If you don't pass work_dir, this runs in the user's real Desktop folder by default (not droidcli's own working directory) - so a bare relative output filename (like the 'output.png' example above) lands there, not in droidcli's own directory. The result includes a \"resolved_work_dir\" field whenever that default applied; pass work_dir explicitly to run somewhere else instead. Runs synchronously and returns captured stdout/stderr/exit_code - encodes can take a while, so raise timeout_ms for large files (default 120000ms).",
		"{\"type\":\"object\",\"properties\":{"
		"\"args\":{\"type\":\"string\",\"description\":\"ffmpeg arguments, e.g. '-y -i input.mp4 output.webm'\"},"
		"\"work_dir\":{\"type\":\"string\",\"description\":\"optional working directory\"},"
		"\"timeout_ms\":{\"type\":\"integer\",\"description\":\"optional timeout in milliseconds, default 120000\"}"
		"},\"required\":[\"args\"]}"});

	tools.push_back(ai::ToolDefinition{
		"get_system_info",
		"Get the host machine droidcli is actually running on right now: OS name, OS version/build, CPU architecture, hostname, username, current working directory, desktop_path (the user's actual Desktop folder, resolved via the OS - not a guess, use this instead of constructing a 'C:\\Users\\<name>\\Desktop'-style path yourself, which breaks for a redirected or localized Desktop folder), and current_datetime (the real current date and time, freshly read every call - your system prompt only has the date/time from when this session started, which goes stale in a long-running session; call this whenever you need to know what time/date it actually is right now, not just at session start). Most fields were already queried once at startup and are in your system prompt, but call this if you need to double-check or report them precisely (e.g. the user asks 'what machine/OS is this', 'where is my Desktop', or 'what's today's date'). Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"search_command_fixes",
		"Search previously recorded fixes for run_command/run_ffmpeg failures - a persistent memory of bugs already solved before, so you don't have to rediscover the same fix from scratch every time. Call this BEFORE attempting a run_command/run_ffmpeg call similar to something that failed earlier (same tool, similar error, similar kind of task), not after. query is matched case-insensitively against the tool name, the broken command, its failure reason, and the lesson text - keep it short and specific (e.g. 'ffmpeg static image' or 'ffmpeg quotes'), not the full failing command.",
		"{\"type\":\"object\",\"properties\":{"
		"\"query\":{\"type\":\"string\",\"description\":\"short search text, e.g. 'ffmpeg static image'\"}"
		"},\"required\":[\"query\"]}"});

	tools.push_back(ai::ToolDefinition{
		"get_hardware_info",
		"Get this machine's local hardware inventory: CPU name/core count, total RAM, GPU adapter name(s), and per-drive disk capacity (total/free bytes for each fixed local drive). This is read-only, local-only information about what the machine is made of - not geolocation, not network scanning, not device control. Only returns real data if the human enabled it at droidcli startup (--enable-hardware-scan); if not, the result reports enabled:false honestly - tell the user that instead of claiming the data isn't available for some other reason. Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"self_status",
		"Check droidcli's own current health before acting or reporting on its capabilities: whether Ollama is reachable right now (cached from a background check, not a fresh call), connector/task counts, and how many of the last 20 log entries were failures. Call this when you're about to say you can't do something, when a tool call just failed and you want to know if it's a one-off or a wider problem (e.g. Ollama down), or when the user asks what state you're in / whether something is working. A false ollama_reachable does not mean you should give up - keep using non-AI tools (run_command, filesystem, connectors) normally and tell the user honestly that the AI backend specifically is degraded. Takes no arguments.",
		"{\"type\":\"object\",\"properties\":{}}"});

	tools.push_back(ai::ToolDefinition{
		"record_command_fix",
		"Record a 'this was broken, this is what fixed it' lesson so future run_command/run_ffmpeg calls (in this or a later session) can look it up via search_command_fixes instead of hitting the same wall again. Call this right after you fix something that failed at least once first - not for a command that worked on the first try, and not for a fix you haven't actually verified worked (an 'ok':true tool result). broken/working should be the actual command/args strings, not a paraphrase; lesson should be one short, specific, reusable sentence (e.g. 'ffmpeg embeds nested double-quotes badly through cmd.exe - avoid quoting filter expressions that don't need it').",
		"{\"type\":\"object\",\"properties\":{"
		"\"tool\":{\"type\":\"string\",\"description\":\"which tool this is about, e.g. 'run_ffmpeg'\"},"
		"\"broken\":{\"type\":\"string\",\"description\":\"the command/args string that failed\"},"
		"\"failure_reason\":{\"type\":\"string\",\"description\":\"what the failure was, e.g. from the tool result's failure_reason field\"},"
		"\"working\":{\"type\":\"string\",\"description\":\"the command/args string that worked instead\"},"
		"\"lesson\":{\"type\":\"string\",\"description\":\"one short, specific, reusable takeaway\"}"
		"},\"required\":[\"tool\",\"broken\",\"working\",\"lesson\"]}"});

	tools.push_back(ai::ToolDefinition{
		"remember_location",
		"Remember a name -> real path mapping so a later turn (or a later session) can recall it instead of re-listing the same directory again - e.g. after list_dir/stat_path resolves what the user meant by 'the green image' or 'Release_1', call this to remember it under that name. path is verified against the real filesystem before it's stored (stat_path must confirm it exists right now) - this will fail with ok:false rather than remember a location that doesn't actually exist, so always resolve the real path first, don't guess one to remember. Calling this again with a name already remembered updates it to the new path rather than creating a duplicate.",
		"{\"type\":\"object\",\"properties\":{"
		"\"name\":{\"type\":\"string\",\"description\":\"short name to remember it under, e.g. 'Release_1' or 'the green image'\"},"
		"\"path\":{\"type\":\"string\",\"description\":\"the real, resolved path (not a guess)\"}"
		"},\"required\":[\"name\",\"path\"]}"});

	tools.push_back(ai::ToolDefinition{
		"get_known_locations",
		"Get a view of the system's locations: where droidcli is right now (current working directory, the user's real Desktop folder), other real OS locations on this machine (Home/profile folder, Documents, Downloads, where installed applications live), and every name -> path mapping remembered so far via remember_location, across this and prior sessions. Call this before falling back to list_dir when the user refers to a place by name (their Home folder, Downloads, 'Release_1 again', 'the folder we made earlier') - if it's a real OS location or already remembered, you can act on the real path directly instead of searching for it again. Takes no arguments.",
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
	if (tool_name == "cancel_task")
	{
		return cancel_task_json(net::extract_json_string_field(arguments_json, "task_id"));
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
	if (tool_name == "list_windows_locations")
	{
		return list_well_known_windows_targets_json();
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
	if (tool_name == "copy_file")
	{
		return copy_file_json(arguments_json);
	}
	if (tool_name == "move_path")
	{
		return move_path_json(arguments_json);
	}
	if (tool_name == "delete_file")
	{
		return delete_file_json(arguments_json);
	}
	if (tool_name == "create_directory")
	{
		return create_directory_json(arguments_json);
	}
	if (tool_name == "read_clipboard")
	{
		return read_clipboard_json();
	}
	if (tool_name == "write_clipboard")
	{
		return write_clipboard_json(arguments_json);
	}
	if (tool_name == "get_system_info")
	{
		return build_system_info_json();
	}
	if (tool_name == "run_ffmpeg")
	{
		return run_ffmpeg_json(arguments_json);
	}
	if (tool_name == "get_hardware_info")
	{
		return build_hardware_info_json();
	}
	if (tool_name == "self_status")
	{
		return build_self_status_json();
	}
	if (tool_name == "search_command_fixes")
	{
		return search_command_fixes_json(arguments_json);
	}
	if (tool_name == "remember_location")
	{
		return remember_location_json(arguments_json);
	}
	if (tool_name == "get_known_locations")
	{
		return list_known_locations_json();
	}
	if (tool_name == "record_command_fix")
	{
		return record_command_fix_json(arguments_json);
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

core::String DroidHost::record_command_fix_json(const core::String& body)
{
	const core::String tool = net::extract_json_string_field(body, "tool");
	const core::String broken = net::extract_json_string_field(body, "broken");
	const core::String failure_reason = net::extract_json_string_field(body, "failure_reason");
	const core::String working = net::extract_json_string_field(body, "working");
	const core::String lesson = net::extract_json_string_field(body, "lesson");

	if (tool.empty() || broken.empty() || working.empty() || lesson.empty())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "tool, broken, working, and lesson are all required") + "}";
	}

	const bool ok = memory_store_.record_lesson(tool, broken, failure_reason, working, lesson);
	append_app_log("lessons", "in", "recorded fix for " + tool + ": " + lesson, ok);
	return "{" + net::json_bool_field("ok", ok) + "}";
}

core::String DroidHost::search_command_fixes_json(const core::String& body) const
{
	const core::String query = net::extract_json_string_field(body, "query");
	const core::Array<CommandLesson> lessons = memory_store_.search_lessons(query);

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", true) << ',';
	stream << "\"lessons\":[";
	for (size_t index = 0; index < lessons.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const CommandLesson& entry = lessons[index];
		stream << '{';
		stream << net::json_string_field("tool", entry.tool) << ',';
		stream << net::json_string_field("broken", entry.broken) << ',';
		stream << net::json_string_field("failure_reason", entry.failure_reason) << ',';
		stream << net::json_string_field("working", entry.working) << ',';
		stream << net::json_string_field("lesson", entry.lesson) << ',';
		stream << net::json_string_field("created_at", entry.created_at);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

core::String DroidHost::remember_location_json(const core::String& body)
{
	const core::String name = net::extract_json_string_field(body, "name");
	const core::String path = net::extract_json_string_field(body, "path");

	if (name.empty() || path.empty())
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "name and path are both required") + "}";
	}
	if (looks_like_placeholder_path(path))
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error",
				"this looks like a placeholder path, not a real one - resolve it with list_dir/stat_path "
				"first, then remember the real path") + "}";
	}

	// Verified against the real filesystem before it's ever remembered - a
	// wrong or stale path would otherwise sit in known_locations looking just
	// as trustworthy as a real one on every later recall (get_known_locations
	// has no way to tell the difference after the fact), so a name is only
	// worth remembering if stat_path can confirm the path exists right now.
	const core::String resolved_path = substitute_bare_desktop_token(path, system_info_.desktop_path);
	const StatResult verification = cli::stat_path(resolved_path);
	if (!verification.ok || !verification.exists)
	{
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "path does not exist - cannot remember a location that isn't real: " + resolved_path) + "}";
	}

	const bool ok = memory_store_.remember_location(name, resolved_path);
	append_app_log("locations", "in", "remembered '" + name + "' -> " + resolved_path, ok);

	core::String response = "{" + net::json_bool_field("ok", ok) + ","
		+ net::json_string_field("resolved_path", resolved_path);
	if (resolved_path != path)
	{
		response += "," + net::json_string_field("original_path", path);
	}
	return response + "}";
}

core::String DroidHost::list_known_locations_json() const
{
	const core::Array<KnownLocation> locations = memory_store_.list_locations();

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", true) << ',';
	// cwd/desktop_path give the model (and the TUI's Locations panel) "where
	// we are right now" alongside "where we've been before" in one call -
	// the "views of the system" this tool exists to answer, not just a bare
	// dump of the known_locations table.
	stream << net::json_string_field("cwd", system_info_.cwd) << ',';
	stream << net::json_string_field("desktop_path", system_info_.desktop_path) << ',';
	// The rest of what droidcli already knows about this machine's layout
	// (Known Folder lookups, cli/system_info.cpp - same "real OS query, never
	// a guessed path" contract as desktop_path), so "known locations" answers
	// more than just what's been explicitly remembered via remember_location.
	// A given entry is skipped entirely if it couldn't be resolved (e.g. no
	// POSIX equivalent of "where installed applications live"), rather than
	// showing an empty/misleading path.
	stream << "\"system_locations\":[";
	{
		bool first_system_location = true;
		const std::pair<const char*, const core::String*> kSystemLocations[] = {
			{"Home", &system_info_.home_path},
			{"Documents", &system_info_.documents_path},
			{"Downloads", &system_info_.downloads_path},
			{"Program Files (apps)", &system_info_.program_files_path},
		};
		for (const auto& location : kSystemLocations)
		{
			if (location.second->empty())
			{
				continue;
			}
			if (!first_system_location)
			{
				stream << ',';
			}
			first_system_location = false;
			stream << '{' << net::json_string_field("name", location.first) << ','
				<< net::json_string_field("path", *location.second) << '}';
		}
	}
	stream << "],";
	stream << "\"known_locations\":[";
	for (size_t index = 0; index < locations.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const KnownLocation& location = locations[index];
		stream << '{';
		stream << net::json_string_field("name", location.name) << ',';
		stream << net::json_string_field("resolved_path", location.resolved_path) << ',';
		stream << net::json_string_field("updated_at", location.updated_at);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

std::unique_ptr<ai::ModelProvider> DroidHost::make_model_provider() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (config_.ai_provider == "anthropic")
	{
		ai::AnthropicConfig anthropic_config;
		anthropic_config.api_key = config_.anthropic_api_key;
		anthropic_config.model = config_.anthropic_model;
		anthropic_config.enabled = true;
		return std::make_unique<ai::AnthropicProvider>(anthropic_config);
	}

	ai::OllamaConfig ollama_config;
	ollama_config.base_url = config_.ollama_url;
	ollama_config.model = config_.ollama_model;
	ollama_config.enabled = true;
	ollama_config.num_ctx = config_.ollama_num_ctx;
	return std::make_unique<ai::OllamaProvider>(ollama_config);
}

void DroidHost::persist_current_settings_locked() const
{
	if (config_.settings_path.empty())
	{
		return;
	}

	// Load first rather than building from scratch - `port` and `api_token`
	// have no authoritative source in config_ (see this method's own
	// declaration comment in host.hpp) and must be carried over exactly as
	// already on disk, not blanked out.
	HostSettings settings;
	load_settings(config_.settings_path, settings);

	settings.enable_ai = config_.enable_ai;
	settings.enable_hardware_scan = config_.enable_hardware_scan;
	settings.ollama_url = config_.ollama_url;
	settings.ollama_model = config_.ollama_model;
	settings.ollama_num_ctx = config_.ollama_num_ctx;
	settings.ai_provider = config_.ai_provider;
	settings.anthropic_model = config_.anthropic_model;
	settings.anthropic_api_key = config_.anthropic_api_key;

	save_settings(config_.settings_path, settings);
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

	// A gated tool call left awaiting a decision (see PendingAgentToolCall)
	// is abandoned rather than silently orphaned if a plain chat message
	// arrives for the same session instead of a POST /api/agent/tool_decision
	// resolving it - otherwise pending_tool_call_ would sit there forever
	// (nothing else ever clears it) and the transcript would carry an
	// assistant turn that implied a tool call with no matching tool result
	// ever appended, which can confuse a later request built from that
	// history. Record the abandonment in-context so the model isn't left
	// wondering what happened to the call it made.
	bool abandoned_pending_call = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (pending_tool_call_.active && pending_tool_call_.session_id == session_id)
		{
			pending_tool_call_ = PendingAgentToolCall{};
			abandoned_pending_call = true;
		}
	}
	if (abandoned_pending_call)
	{
		record_agent_message(session_id, ai::ChatRole::System,
			"(A previously pending tool call in this session was abandoned - the user sent a new "
			"message instead of approving or declining it. If it's still relevant, call the tool again.)");
		// record_agent_message() only persists to the transcript/MemoryStore
		// (session history, GET /api/agent/history) - logged here too so
		// this shows up in the durable logs/log.jsonl trail alongside every
		// other chat-loop event, not just in a session replay.
		append_app_log("chat", "out", "pending tool call abandoned (new message arrived before it was approved/declined)", false, session_id);
	}

	append_app_log("chat", "in", "user: " + user_message, true, session_id);

	if (!config_.enable_ai)
	{
		append_app_log("chat", "out", "rejected: AI is disabled (--no-ai)", false, session_id);
		return "{" + net::json_bool_field("ok", false) + ","
			+ net::json_string_field("error", "AI is disabled (--no-ai)") + "}";
	}

	bool seed_system_prompt = false;
	core::String system_prompt_text;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (agent_transcript_.empty() && !config_.system_prompt.empty())
		{
			// Appends the concrete count from the startup scan (see initialize())
			// so the model is told a fact, not a capability description it might
			// discount - "you have 74 apps indexed" is harder to hedge on than
			// "you can look up installed apps".
			seed_system_prompt = true;
			system_prompt_text = config_.system_prompt
				+ " Right now this index has " + std::to_string(installed_apps_.size())
				+ " installed applications in it."
				// Labeled explicitly as "as of the start of this session," not
				// "the current time" - this is only sent once, when the
				// transcript is empty, so it goes stale in a long-running
				// session. call get_system_info for a fresh read instead of
				// assuming this one is still current.
				+ " As of the start of this session, the date and time was "
				+ make_full_log_timestamp() + " - call get_system_info if you "
				"need the actual current date/time later in a long session, "
				"since this won't update on its own.";
		}
	}

	// Captured before the new user message is appended below - this is what
	// extract_proposed_command() checks to see whether the assistant itself
	// just proposed a command and asked permission to run it. Stops at the
	// first Assistant entry found scanning backward (the normal case: the
	// transcript always alternates, so this is simply the reply the user is
	// responding to); breaks without matching on a User entry first, which
	// only happens for a brand-new/empty transcript.
	core::String previous_assistant_text;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (auto it = agent_transcript_.rbegin(); it != agent_transcript_.rend(); ++it)
		{
			if (it->role == ai::ChatRole::Assistant)
			{
				previous_assistant_text = it->content;
				break;
			}
			if (it->role == ai::ChatRole::User)
			{
				break;
			}
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

	// Coded against ai::ModelProvider, not any concrete provider type
	// directly - make_model_provider() selects Ollama vs Anthropic from
	// config_.ai_provider, nothing below this line changes either way. See
	// "Second ModelProvider" in ARCHITECTURE.md and src/ai/model_provider.hpp.
	const std::unique_ptr<ai::ModelProvider> provider_ptr = make_model_provider();
	const ai::ModelProvider& provider = *provider_ptr;

	// Deterministic command-confirmation shortcut (Phase 14, ARCHITECTURE.md):
	// if the assistant's own last message proposed a concrete command and
	// explicitly asked permission to run it, and this message is nothing but
	// a bare "yes", the user's affirmative IS that permission - execute the
	// exact proposed command directly rather than ask the (unreliable) model
	// to decide all over again. Motivated by a real transcript where this
	// exact "yes" got an empty model response, then a false "I can only
	// execute one command at a time" claim, and the tool was never called.
	// This mirrors try_quick_open_json's existing precedent of bypassing the
	// LLM for a narrow, high-confidence, deterministically-recognized case.
	const intent::PendingCommand proposed_command = intent::extract_proposed_command(previous_assistant_text);
	if (proposed_command.matched && intent::is_bare_affirmative(user_message))
	{
		const core::String arguments_json = proposed_command.tool == "run_ffmpeg"
			? ("{" + net::json_string_field("args", proposed_command.args) + "}")
			: ("{" + net::json_string_field("command", proposed_command.args) + "}");
		const core::String tool_result = execute_agent_tool(proposed_command.tool, arguments_json);
		append_app_log("chat", "out",
			"confirmed proposed command - executing " + proposed_command.tool + "(" + arguments_json + ") -> " + tool_result,
			true, session_id);
		record_agent_message(session_id, ai::ChatRole::Tool, tool_result);

		core::Array<PendingToolActionRecord> seeded_actions;
		seeded_actions.push_back(PendingToolActionRecord{proposed_command.tool, arguments_json, tool_result});

		// hop=1, not 0: this deterministic execution already covers what
		// would have been hop 0's "decide which tool to call" step. The
		// model's next hop only needs to react to the result already in the
		// transcript - report success, or (via Phase 12's retry-on-failure
		// nudge, which reads the same seeded_actions to find the last
		// action) retry with a corrected command if it failed.
		return run_agent_tool_loop(session_id, tools, provider, 1, {}, 0, seeded_actions);
	}

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

	const core::Array<ai::ToolDefinition> tools = agent_tool_definitions();
	const std::unique_ptr<ai::ModelProvider> provider_ptr = make_model_provider();
	const ai::ModelProvider& provider = *provider_ptr;

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
	// Raised from 5 (Phase 12): the command-failure auto-retry mechanism
	// below can consume up to kMaxCommandRetryNudges hops on its own, on top
	// of the pre-existing fabrication/capability-denial nudges and the
	// hop(s) actually spent calling tools - 5 left no room for a real
	// multi-attempt retry-until-it-works loop, which is the whole point of
	// this phase. See "Phase 12" in ARCHITECTURE.md.
	constexpr int kMaxHops = 9;
	core::String final_assistant_text;
	bool budget_exhausted = false;
	// Capped at 1, not "every hop until the budget runs out": a model that
	// fabricates once and self-corrects after a single nudge is common: a
	// model that fabricates the *same* claim again right after being told
	// it didn't happen is not going to be talked out of it by repeating the
	// same nudge three more times - it just burns the hop budget on
	// nudges instead of ever attempting the real tool call, and in
	// practice degrades a small local model's output further (observed:
	// four nudges in one turn led to the model echoing literal
	// "assistant\n\n" role-token fragments into its own completion).
	int unverified_claim_nudge_count = 0;
	constexpr int kMaxUnverifiedClaimNudges = 1;

	// A failed run_command/run_ffmpeg call must not just be reported back to
	// the user with "want me to try again?" - the model has (most of) a full
	// hop budget and the exact failure_reason already; it should read the
	// error and retry with a corrected command itself, same as a human
	// operator debugging a broken command line would. Capped (not unbounded)
	// for the same reason kMaxUnverifiedClaimNudges is: a command that's
	// wrong in a way the model can't diagnose from the error alone will just
	// keep failing, and burning the whole hop budget on that guarantees an
	// unhelpful non-answer instead of an honest "I tried N times, here's the
	// last real error." See "Phase 12" in ARCHITECTURE.md - motivated by a
	// real transcript where the model asked permission to retry four
	// separate times across four separate user messages instead of once,
	// automatically, within the turn it already had the error in front of it.
	int command_retry_nudge_count = 0;
	constexpr int kMaxCommandRetryNudges = 3;

	// A non-empty resume_calls means we're continuing a hop that was already
	// fetched from the model before an earlier call in it paused for
	// approval - finish whatever's left in it before asking the model for
	// anything new. A fresh agent_turn() always passes an empty array here,
	// so this block is skipped entirely on a normal first call.
	if (!resume_calls.empty())
	{
		for (size_t call_index = resume_call_index; call_index < resume_calls.size(); ++call_index)
		{
			ai::ToolCall& call = resume_calls[call_index];
			if (tool_call_requires_approval(call.name))
			{
				// Never propose a yes/no for an open_application call that's
				// guaranteed to fail - resolve first (rewriting call in
				// place on success), and if nothing resolves, skip the
				// approval prompt entirely and record it as an immediately
				// failed action instead. See "Never propose an unresolvable
				// action" in ARCHITECTURE.md.
				core::String precheck_failure_json;
				if (!precheck_and_resolve_gated_call(call, precheck_failure_json))
				{
					append_app_log("chat", "out",
						"tool " + call.name + "(" + call.arguments_json + ") -> " + precheck_failure_json, false, session_id);
					record_agent_message(session_id, ai::ChatRole::Tool, precheck_failure_json);
					actions.push_back(PendingToolActionRecord{call.name, call.arguments_json, precheck_failure_json});
					continue;
				}

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

		// A tool-use-tuned local model can come back with neither assistant
		// text nor a tool call for a given hop - not a transport/HTTP
		// failure (parse_ollama_chat_response still reports http_success),
		// just nothing to act on or show. Seen in practice with
		// llama3-groq-tool-use, and near-always transient: retrying the
		// identical request resolves it far more often than not (this is
		// exactly what a user manually re-typing the same message was doing
		// before this loop existed). Bounded separately from kMaxHops - an
		// empty reply never produced a tool call, so it shouldn't eat into
		// that budget the way a real hop does.
		constexpr int kMaxEmptyRetries = 2;
		ai::ProviderResponse response;
		core::String last_response_body;
		const auto hop_wall_clock_start = std::chrono::steady_clock::now();
		int attempts_used = 0;
		for (int attempt = 0; ; ++attempt)
		{
			attempts_used = attempt + 1;
			int32_t status_code = 0;
			core::String response_body;
			const bool transport_ok = language_ai_transport_.post_json
				? language_ai_transport_.post_json(request.url, request.body, request.headers, status_code, response_body)
				: false;

			response = provider.parse_response(status_code, response_body, transport_ok);
			last_response_body = response_body;

			if (!response.transport_ok || !response.http_success)
			{
				break;
			}
			if (!response.assistant_message.empty() || !response.tool_calls.empty())
			{
				break;
			}
			if (attempt >= kMaxEmptyRetries)
			{
				// Capped in the log line - the raw body is diagnostic, not
				// something that needs to round-trip in full.
				append_app_log("chat", "out",
					"hop " + std::to_string(hop) + ": model returned neither text nor a tool call after "
						+ std::to_string(kMaxEmptyRetries + 1) + " attempts, raw response: "
						+ last_response_body.substr(0, 500),
					false, session_id);
				break;
			}
			append_app_log("chat", "out",
				"hop " + std::to_string(hop) + ": model returned neither text nor a tool call, retrying ("
					+ std::to_string(attempt + 1) + "/" + std::to_string(kMaxEmptyRetries) + ")",
				false, session_id);
		}
		const int64_t hop_wall_clock_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - hop_wall_clock_start).count();

		// Structured telemetry for this hop, on a channel named after the
		// active provider (config_.ai_provider - "ollama" or "anthropic")
		// so it doesn't crowd the "chat" channel's human-summary lines above,
		// and so switching providers (Phase 32) doesn't leave every log line
		// mislabeled "ollama". Wall-clock latency (spans every retry this hop
		// needed) plus, whenever the response actually carried it, the
		// provider's own generation timing and token counts. See "Ollama
		// telemetry"/"Second ModelProvider" in ARCHITECTURE.md.
		const core::String active_model = active_model_name();
		const core::String model_metrics_fields =
			net::json_string_field("model", active_model) + ","
			+ "\"hop\":" + std::to_string(hop) + ","
			+ "\"attempts\":" + std::to_string(attempts_used) + ","
			+ "\"wall_clock_ms\":" + std::to_string(hop_wall_clock_ms) + ","
			+ "\"model_total_ms\":" + std::to_string(response.total_duration_ms) + ","
			+ "\"model_eval_ms\":" + std::to_string(response.eval_duration_ms) + ","
			+ "\"prompt_tokens\":" + std::to_string(response.prompt_tokens) + ","
			+ "\"completion_tokens\":" + std::to_string(response.completion_tokens) + ","
			+ net::json_string_field("done_reason", response.done_reason) + ","
			+ "\"tool_calls\":" + std::to_string(response.tool_calls.size());
		append_app_log(config_.ai_provider, "out",
			"hop " + std::to_string(hop) + " (" + std::to_string(attempts_used) + " attempt(s), "
				+ std::to_string(hop_wall_clock_ms) + "ms)",
			response.transport_ok && response.http_success, session_id, model_metrics_fields);

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
			// Caught an unverified action claim (a promise never followed
			// through, or a past-tense "I've done X" with nothing to back
			// it up). Only flagged when no *matching* action this turn
			// actually succeeded yet - a truthful summary of a tool call
			// that already ran earlier in the same turn ("I've successfully
			// created it" right after run_ffmpeg returned ok:true) must
			// never be second-guessed.
			//
			// Only a mutating/gated tool's success counts as "matching" here
			// (tool_call_requires_approval - copy_file, write_file, run_command,
			// open_application, etc.), not any read-only success. A real
			// transcript showed this scan take *any* ok:true action this turn
			// as license to trust the claim - list_connectors returning an
			// empty list (trivially ok:true) was enough to wave through "I've
			// successfully copied the file," and separately, list_open_windows
			// succeeding was enough to wave through "The Recycle Bin is
			// already running" (unsupported by the window list it just
			// returned) and "I called list_dir, then run_command to create
			// it" (no run_command call exists anywhere in that session). A
			// claim of a completed action can only be backed by an action that
			// actually changes something - a read-only lookup succeeding is
			// never evidence for it, no matter what the claim is about.
			bool a_tool_call_already_succeeded_this_turn = false;
			for (const PendingToolActionRecord& action : actions)
			{
				if (!tool_call_requires_approval(action.tool))
				{
					continue;
				}
				bool action_ok = false;
				if (net::extract_json_bool_field(action.result_json, "ok", action_ok) && action_ok)
				{
					a_tool_call_already_succeeded_this_turn = true;
					break;
				}
			}
			// The role-leak check runs regardless of a_tool_call_already_succeeded_this_turn -
			// unlike an unverified claim, leaked-role garbled text isn't a
			// legitimate truthful summary under any circumstance, tool call or
			// not, so a prior success in this turn never excuses it.
			const bool looks_fabricated =
				(!a_tool_call_already_succeeded_this_turn && looks_like_unverified_action_claim(response.assistant_message))
				|| looks_like_degenerate_role_leak(response.assistant_message);

			if (looks_fabricated && hop < kMaxHops - 1
				&& unverified_claim_nudge_count < kMaxUnverifiedClaimNudges)
			{
				// There's hop budget and nudge budget left - give the model
				// one shot at either actually calling the tool or being
				// honest about not having done so, instead of treating the
				// claim as the final answer.
				++unverified_claim_nudge_count;
				append_app_log("chat", "out",
					"hop " + std::to_string(hop) + ": assistant claimed an action with no successful tool call to back it up, nudging it to correct itself",
					false, session_id);
				record_agent_message(session_id, ai::ChatRole::System, kUnverifiedActionClaimNudge);
				continue;
			}

			if (looks_fabricated)
			{
				// The nudge already fired once this turn (or there's no hop
				// budget left for one) and the model is STILL claiming an
				// unbacked action - do not let that fabrication reach the
				// user under any circumstance, even as a "final" answer.
				// Override with an honest refusal rather than trusting the
				// model's own text this one time; the model gets to try
				// again on the user's next message instead of digging the
				// same hole deeper across more nudges (which, in practice,
				// degrades a small local model's output further rather
				// than fixing anything - see kMaxUnverifiedClaimNudges).
				append_app_log("chat", "out",
					"hop " + std::to_string(hop) + ": assistant still claiming an unbacked action after nudging - overriding with an honest refusal instead of passing the fabrication through",
					false, session_id);
				final_assistant_text =
					"I wasn't actually able to complete this - I kept describing the action "
					"instead of calling the tool for it, and didn't correct that after being "
					"asked to. Please try again, or rephrase the request.";
				break;
			}

			// Was the most recent action this turn a retriable tool call that
			// failed? If so, the model landing here with plain text and no
			// new tool_calls means it's either asking the user's permission
			// to retry, or has otherwise stalled - in both cases it already
			// has the real error in front of it and hop budget to spare, so
			// it should retry itself rather than hand the decision back to
			// the user. Originally just run_command/run_ffmpeg (Phase 12);
			// extended to the filesystem tools too (Phase 23) - a real
			// transcript showed a failed read_file/write_file only ever get
			// the "don't lie about it" fabrication nudge, never a push to
			// actually retry with a corrected path, so it took several user
			// turns to even get the real Desktop path via get_system_info.
			core::String failed_command_tool;
			core::String failed_command_reason;
			bool last_action_was_failed_command = false;
			if (!actions.empty())
			{
				const PendingToolActionRecord& last_action = actions.back();
				static const char* const kRetriableTools[] = {
					"run_command", "run_ffmpeg",
					"read_file", "write_file", "copy_file", "move_path", "delete_file", "create_directory",
					// Added alongside the pre-approval resolution precheck
					// (Phase 42) - an unresolved open_application name is
					// exactly the "model has enough information to fix
					// itself" shape (the error names the exact tiers that
					// were tried and points at list_windows_locations/
					// find_application), the same reasoning that already
					// applies to the filesystem tools.
					"open_application"
				};
				bool is_retriable_tool = false;
				for (const char* retriable_tool : kRetriableTools)
				{
					if (last_action.tool == retriable_tool)
					{
						is_retriable_tool = true;
						break;
					}
				}
				if (is_retriable_tool)
				{
					bool action_ok = true;
					net::extract_json_bool_field(last_action.result_json, "ok", action_ok);
					if (!action_ok)
					{
						last_action_was_failed_command = true;
						failed_command_tool = last_action.tool;
						failed_command_reason = net::extract_json_string_field(last_action.result_json, "failure_reason");
						if (failed_command_reason.empty())
						{
							failed_command_reason = net::extract_json_string_field(last_action.result_json, "error");
						}
					}
				}
			}

			const bool denies_capability = looks_like_capability_denial(response.assistant_message);

			if ((last_action_was_failed_command || denies_capability)
				&& hop < kMaxHops - 1
				&& command_retry_nudge_count < kMaxCommandRetryNudges)
			{
				++command_retry_nudge_count;
				const int retries_left = kMaxCommandRetryNudges - command_retry_nudge_count;
				const core::String nudge = last_action_was_failed_command
					? ("Your last " + failed_command_tool + " call failed: " + failed_command_reason
						+ ". Do not ask the user whether to retry - analyze this exact error yourself "
						"and call " + failed_command_tool + " again right now with corrected input (a "
						"corrected command, path, or argument, whichever the error points to) - use "
						"list_dir/stat_path/get_system_info first if you need the real path/username "
						"rather than guessing again. You have " + std::to_string(retries_left)
						+ " automatic retr" + (retries_left == 1 ? "y" : "ies")
						+ " left after this one before you must report the real error honestly instead of "
						"trying again.")
					: core::String(kCapabilityDenialNudge);
				append_app_log("chat", "out",
					"hop " + std::to_string(hop) + ": " + (last_action_was_failed_command
						? "last command failed - nudging the model to retry with a corrected command instead of asking permission"
						: "assistant falsely denied having command-execution capability - nudging it to correct itself"),
					false, session_id);
				record_agent_message(session_id, ai::ChatRole::System, nudge);
				continue;
			}

			if (denies_capability)
			{
				// Retry/nudge budget exhausted and it's STILL falsely denying
				// capability - tell the user the truth rather than let a lie
				// about the agent's own abilities reach them unchallenged.
				append_app_log("chat", "out",
					"hop " + std::to_string(hop) + ": assistant still falsely denying command-execution capability after nudging",
					false, session_id);
				final_assistant_text =
					"Something went wrong here - I incorrectly told you I can't execute commands, "
					"which isn't true (I do have run_command/run_ffmpeg/file tools). Please try "
					"rephrasing your request; if this keeps happening, it's worth reporting as a bug.";
				break;
			}

			if (last_action_was_failed_command)
			{
				// Retry budget exhausted and the command is still failing -
				// report the real last error honestly instead of silently
				// giving up or asking the user to retry it themselves.
				append_app_log("chat", "out",
					"hop " + std::to_string(hop) + ": command retry budget exhausted, reporting the last real error",
					false, session_id);
				final_assistant_text =
					"I tried " + failed_command_tool + " " + std::to_string(kMaxCommandRetryNudges + 1)
					+ " time(s) and it kept failing. Last error: " + failed_command_reason
					+ ". I'm stopping here rather than keep guessing - let me know if you have more "
					"details (the exact format/filter you want) and I'll try again.";
				break;
			}

			final_assistant_text = response.assistant_message;
			break;
		}

		bool paused = false;
		size_t call_index = 0;
		for (; call_index < response.tool_calls.size(); ++call_index)
		{
			ai::ToolCall& call = response.tool_calls[call_index];
			if (tool_call_requires_approval(call.name))
			{
				// Never propose a yes/no for an open_application call that's
				// guaranteed to fail - resolve first (rewriting call in
				// place on success), and if nothing resolves, skip the
				// approval prompt entirely and record it as an immediately
				// failed action instead. See "Never propose an unresolvable
				// action" in ARCHITECTURE.md.
				core::String precheck_failure_json;
				if (!precheck_and_resolve_gated_call(call, precheck_failure_json))
				{
					append_app_log("chat", "out",
						"tool " + call.name + "(" + call.arguments_json + ") -> " + precheck_failure_json, false, session_id);
					record_agent_message(session_id, ai::ChatRole::Tool, precheck_failure_json);
					actions.push_back(PendingToolActionRecord{call.name, call.arguments_json, precheck_failure_json});
					continue;
				}

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
		// A single, definitive false-flagged log line at the one place this
		// placeholder is ever substituted - a catch-all so every way a turn
		// can end without a usable reply lands in logs/log.jsonl as a
		// failure, even if the more specific path that caused it (retry
		// exhaustion, budget exhaustion) didn't already log its own entry.
		append_app_log("chat", "out",
			"turn ended without a usable reply (" + core::String(budget_exhausted ? "tool-call budget exhausted" : "empty model response") + ")",
			false, session_id);
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

core::String DroidHost::display_arguments_with_full_paths(const core::String& tool_name, const core::String& arguments_json) const
{
	const auto resolve_field = [&](const core::String& json, const core::String& field_name) -> core::String
	{
		const core::String raw_path = net::extract_json_string_field(json, field_name);
		if (raw_path.empty() || looks_like_placeholder_path(raw_path))
		{
			return json;
		}

		const core::String resolved = default_bare_filename_to_desktop(
			substitute_bare_desktop_token(raw_path, system_info_.desktop_path), system_info_.desktop_path);
		if (looks_like_invented_desktop_path(resolved, system_info_.desktop_path))
		{
			return json;
		}

		std::error_code error;
		const core::String full_path = std::filesystem::absolute(std::filesystem::path(resolved), error).string();
		if (error || full_path.empty())
		{
			return json;
		}

		return replace_json_string_field_value(json, field_name, full_path);
	};

	if (tool_name == "write_file" || tool_name == "delete_file" || tool_name == "create_directory")
	{
		return resolve_field(arguments_json, "path");
	}
	if (tool_name == "copy_file" || tool_name == "move_path")
	{
		return resolve_field(resolve_field(arguments_json, "source_path"), "destination_path");
	}
	if (tool_name == "open_application")
	{
		// Deliberately narrower than resolve_field above: open_application's
		// "path_or_name" is very often a bare app name ("notepad", "Memory",
		// "chrome") to be searched for, not a path at all - running that
		// through default_bare_filename_to_desktop would wrongly treat it as
		// a Desktop-relative filename. Only rewrite when the value already
		// contains a path separator (looks_like_path, the same check
		// launch_application itself uses to decide whether to skip App Paths
		// registry lookup) - a bare name is shown exactly as given.
		const core::String raw_value = net::extract_json_string_field(arguments_json, "path_or_name");
		if (raw_value.empty() || !looks_like_path(raw_value))
		{
			return arguments_json;
		}
		std::error_code error;
		const core::String full_path = std::filesystem::absolute(std::filesystem::path(raw_value), error).string();
		if (error || full_path.empty())
		{
			return arguments_json;
		}
		return replace_json_string_field_value(arguments_json, "path_or_name", full_path);
	}
	return arguments_json;
}

core::String DroidHost::build_pending_tool_call_response(
	const core::String& session_id,
	const ai::ToolCall& call,
	const core::Array<PendingToolActionRecord>& actions_so_far) const
{
	// Visibility aid, not a gate (tool_call_requires_approval is still the
	// only real one): run_command/run_ffmpeg is the single largest blast-
	// radius capability droidcli has, and the approval prompt otherwise just
	// shows the raw string - flag an unambiguously destructive shape
	// (recursive delete, disk-formatting, forced broad process/service
	// kills, shutdown/reboot) so a human skimming past a routine "yes" is
	// more likely to actually stop and read this particular one.
	bool looks_destructive = false;
	if (call.name == "run_command")
	{
		looks_destructive = looks_like_destructive_command(net::extract_json_string_field(call.arguments_json, "command"));
	}
	else if (call.name == "run_ffmpeg")
	{
		looks_destructive = looks_like_destructive_command(net::extract_json_string_field(call.arguments_json, "args"));
	}

	std::ostringstream stream;
	stream << '{';
	stream << net::json_bool_field("ok", true) << ',';
	stream << net::json_string_field("session_id", session_id) << ',';
	stream << "\"pending_tool_call\":{";
	stream << net::json_string_field("tool", call.name) << ',';
	stream << net::json_string_field("arguments_json", display_arguments_with_full_paths(call.name, call.arguments_json)) << ',';
	stream << net::json_bool_field("looks_destructive", looks_destructive);
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
		stream << net::json_string_field("arguments_json", display_arguments_with_full_paths(action.tool, action.arguments_json)) << ',';
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

void DroidHost::log_quick_open_event(const core::String& summary, const bool success)
{
	append_app_log("quick_open", "out", summary, success);
}

void DroidHost::log_chat_entry(const core::String& role, const core::String& text)
{
	append_app_log("chat", role, text, role != "error");
}

} // namespace droidcli::cli
