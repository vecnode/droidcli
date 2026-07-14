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
#include <exception>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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
		if (!row.id.empty())
		{
			rows.push_back(row);
		}
	}
	return rows;
}

std::vector<std::string> parse_log_lines(const std::string& json)
{
	std::vector<std::string> lines;
	for (const std::string& object : extract_json_object_array(json, "entries"))
	{
		const std::string timestamp = net::extract_json_string_field(object, "timestamp");
		const std::string channel = net::extract_json_string_field(object, "channel");
		const std::string direction = net::extract_json_string_field(object, "direction");
		const std::string summary = net::extract_json_string_field(object, "summary");
		lines.push_back("[" + timestamp + "] [" + channel + "] [" + direction + "] " + summary);
	}
	return lines;
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
	std::vector<std::string> log_lines;
};

// One line of the chat panel: who said it (drives color/weight) and the text.
struct ChatEntry {
	std::string role; // "user" | "assistant" | "tool" | "error" | "info"
	std::string text;
};

// Extracts DroidHost::agent_turn()'s response ({"ok":bool,"assistant":"...",
// "actions":[{"tool":"...","arguments_json":"...","result_json":"..."}],
// "error":"..."}) into chat lines: the assistant's reply plus one line per
// tool call made along the way.
std::vector<ChatEntry> parse_agent_turn_response(const std::string& json)
{
	std::vector<ChatEntry> entries;
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

	const std::string assistant = net::extract_json_string_field(json, "assistant");
	entries.push_back(ChatEntry{"assistant", assistant.empty() ? "(no reply)" : assistant});
	return entries;
}

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
	std::vector<std::string> log_lines;
	std::vector<ChatEntry> chat_entries;
	std::string chat_input_text;
	bool agent_turn_in_flight = false;
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
		Elements rows;
		rows.push_back(hbox({
			text("ID") | size(WIDTH, EQUAL, 22),
			text("CONNECTOR") | size(WIDTH, EQUAL, 16),
			text("COMMAND") | size(WIDTH, EQUAL, 10),
			text("STATUS"),
		}) | bold);
		for (const TaskRow& task : tasks)
		{
			rows.push_back(hbox({
				text(task.id) | size(WIDTH, EQUAL, 22),
				text(task.connector_id) | size(WIDTH, EQUAL, 16),
				text(task.command) | size(WIDTH, EQUAL, 10),
				text(task.status),
			}));
		}
		if (tasks.empty())
		{
			rows.push_back(text("(no tasks queued)") | dim);
		}
		return vbox(rows) | frame | flex;
	});

	Component log_view = Renderer([&]() -> Element
	{
		Elements lines;
		const size_t max_visible_lines = 200;
		const size_t start = log_lines.size() > max_visible_lines ? log_lines.size() - max_visible_lines : 0;
		for (size_t index = start; index < log_lines.size(); ++index)
		{
			lines.push_back(text(log_lines[index]));
		}
		if (lines.empty())
		{
			lines.push_back(text("(no log entries yet)") | dim);
		}
		return vbox(lines) | yframe | flex;
	});

	Component chat_log_view = Renderer([&]() -> Element
	{
		Elements lines;
		for (const ChatEntry& entry : chat_entries)
		{
			if (entry.role == "user")
			{
				lines.push_back(text("you: " + entry.text) | bold | color(Color::Cyan));
			}
			else if (entry.role == "assistant")
			{
				lines.push_back(text("agent: " + entry.text) | color(Color::Green));
			}
			else if (entry.role == "tool")
			{
				lines.push_back(text("  " + entry.text) | dim | color(Color::Yellow));
			}
			else if (entry.role == "error")
			{
				lines.push_back(text("error: " + entry.text) | color(Color::Red));
			}
			else
			{
				lines.push_back(text(entry.text) | dim);
			}
		}
		if (agent_turn_in_flight)
		{
			lines.push_back(text("agent: (thinking...)") | dim);
		}
		if (lines.empty())
		{
			lines.push_back(text("(no messages yet - type below and press Enter)") | dim);
		}
		return vbox(lines) | yframe | flex;
	});

	// Submit-on-Enter: calls DroidHost::agent_turn() directly (in-process, no
	// HTTP/token needed) on this event thread only - never from the poller
	// thread - consistent with how l/s already block briefly below.
	//
	// Before routing to agent_turn() (which needs a working, model-loaded
	// Ollama to decide anything), a small hardcoded state machine checks
	// Ollama's setup status in-process and, if it isn't ready, walks the user
	// through install/start/pull instead - this path must work even when
	// Ollama/the LLM is completely unreachable.
	//
	// Everything in this handler runs inside a try/catch: a network failure
	// calling Ollama (or any other exception raised while handling a chat
	// submit) must always surface as an "error" chat entry, never propagate
	// out of this FTXUI event handler - FTXUI's own loop does not catch
	// exceptions, so an uncaught one here would unwind into std::terminate()
	// and kill the whole process/terminal, not just fail the one turn.
	Component chat_input = Input(&chat_input_text, "type a message, Enter to send...");
	chat_input = CatchEvent(chat_input, [&](Event event) -> bool
	{
		if (event == Event::Return)
		{
			if (chat_input_text.empty() || agent_turn_in_flight)
			{
				return true;
			}
			const std::string message = chat_input_text;
			chat_input_text.clear();
			chat_entries.push_back(ChatEntry{"user", message});
			agent_turn_in_flight = true;

			try
			{
				const std::string lower_message = to_lower(trim(message));
				const OllamaSetupStatus setup_status = parse_ollama_setup_status(host.ollama_setup_status_json());
				const OllamaSetupState setup_state = compute_setup_state(setup_status);

				if (setup_state == OllamaSetupState::NeedsInstall)
				{
					if (lower_message == "install ollama" || lower_message == "install")
					{
						chat_entries.push_back(ChatEntry{"info", "Installing Ollama, this can take a few minutes..."});
						const std::string result_json = host.install_ollama();
						bool ok = false;
						net::extract_json_bool_field(result_json, "ok", ok);
						if (ok)
						{
							chat_entries.push_back(ChatEntry{"info",
								"Ollama installed. Type a message to continue - you'll be asked to start it next."});
						}
						else
						{
							const std::string error = net::extract_json_string_field(result_json, "error");
							const std::string stderr_text = net::extract_json_string_field(result_json, "stderr");
							chat_entries.push_back(ChatEntry{"error",
								"Ollama install failed: " + (error.empty() ? stderr_text : error)});
						}
					}
					else
					{
						chat_entries.push_back(ChatEntry{"info",
							"Ollama isn't installed. Type 'install ollama' to install it automatically "
							"(via winget), or install it yourself from ollama.com."});
					}
				}
				else if (setup_state == OllamaSetupState::NeedsStart)
				{
					if (lower_message == "start ollama" || lower_message == "start")
					{
						chat_entries.push_back(ChatEntry{"info", "Starting Ollama..."});
						const std::string result_json = host.start_ollama();
						bool ok = false;
						net::extract_json_bool_field(result_json, "ok", ok);
						bool online = false;
						net::extract_json_bool_field(result_json, "online", online);
						if (ok && online)
						{
							chat_entries.push_back(ChatEntry{"info", "Ollama is running. Type a message to continue."});
						}
						else if (ok)
						{
							chat_entries.push_back(ChatEntry{"error",
								"Ollama process started but is not yet reachable at its configured URL. "
								"Try again in a moment."});
						}
						else
						{
							chat_entries.push_back(ChatEntry{"error",
								"Failed to start Ollama: " + net::extract_json_string_field(result_json, "error")});
						}
					}
					else
					{
						chat_entries.push_back(ChatEntry{"info",
							"Ollama is installed but not running. Type 'start ollama' to launch it."});
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
						chat_entries.push_back(ChatEntry{"info",
							"Pulled models: " + (models_text.empty() ? std::string("(none)") : models_text)});
					}
					else if (lower_message.empty() || lower_message == "install" || lower_message == "install ollama"
						|| lower_message == "start" || lower_message == "start ollama")
					{
						chat_entries.push_back(ChatEntry{"info",
							"No usable Ollama model found. Type a model name to download it "
							"(e.g. 'llama3.2', 'qwen2.5', 'mistral-nemo') or 'list' to see currently pulled models."});
					}
					else
					{
						chat_entries.push_back(ChatEntry{"info",
							"Downloading model '" + message + "', this can take a while..."});
						std::ostringstream pull_body;
						pull_body << '{' << net::json_string_field("model", message) << '}';
						const std::string result_json = host.pull_ollama_model(pull_body.str());
						bool ok = false;
						net::extract_json_bool_field(result_json, "ok", ok);
						if (ok)
						{
							chat_entries.push_back(ChatEntry{"info",
								"Model '" + message + "' downloaded and set as the active model. "
								"Type a message to continue."});
						}
						else
						{
							chat_entries.push_back(ChatEntry{"error",
								"Failed to pull model '" + message + "': "
								+ net::extract_json_string_field(result_json, "error")});
						}
					}
				}
				else
				{
					std::ostringstream request_body;
					request_body << '{' << net::json_string_field("message", message) << '}';
					const std::string response_json = host.agent_turn(request_body.str());

					for (const ChatEntry& entry : parse_agent_turn_response(response_json))
					{
						chat_entries.push_back(entry);
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

			agent_turn_in_flight = false;
			return true;
		}
		return false;
	});

	// Only connector_menu and chat_input are focusable (Menu/Input); Tab
	// cycles between them. Renderer-only panels (tasks/log/chat_log) never
	// take focus, so they're rendered but not part of this container.
	Component interactive = Container::Vertical({ connector_menu, chat_input });
	Component root_container = interactive;

	Component full_ui = Renderer(root_container, [&]() -> Element
	{
		Element connectors_panel = window(text(" Connectors  (l=launch, s=stop) "), connector_menu->Render()) | flex;
		Element tasks_panel = window(text(" Tasks "), tasks_view->Render()) | flex;
		Element log_panel = window(text(" App Log "), log_view->Render()) | flex;
		Element chat_panel = window(text(" Agent Chat  (Tab to focus, Enter to send) "),
			vbox({ chat_log_view->Render() | flex, separator(), chat_input->Render() })) | flex;
		return vbox({
			text(status_line) | bold,
			hbox({
				vbox({ connectors_panel, tasks_panel }) | flex,
				vbox({ log_panel, chat_panel }) | flex,
			}) | flex,
			text("Tab: switch focus   (connectors focused) q: quit   l: launch   s: stop   j/k/arrows: move   Ctrl+C: quit anytime") | dim,
		});
	});

	full_ui = CatchEvent(full_ui, [&](Event event) -> bool
	{
		if (event == Event::Custom)
		{
			std::lock_guard<std::mutex> lock(polled.mutex);
			connectors = polled.connectors;
			tasks = polled.tasks;
			log_lines = polled.log_lines;
			rebuild_connector_entries();
			return true;
		}

		if (event == Event::CtrlC)
		{
			running_flag = false;
			screen.Exit();
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

		return false;
	});

	// An exception escaping a std::thread's entry function calls
	// std::terminate() immediately - it does NOT propagate to poller.join()
	// below, so a try/catch around screen.Loop() would not protect this
	// thread. Defense in depth: catch here too, log to stderr, and keep
	// polling rather than letting one bad iteration kill the process.
	std::thread poller([&]()
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
				std::vector<std::string> next_log = parse_log_lines(host.build_app_log_json());

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
	});

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

	return 0;
}

} // namespace droidcli::cli
