#include "tui.hpp"

#include "net/json.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <chrono>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace metaagent::cli {
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

	Component left_column = Container::Vertical({ connector_menu, tasks_view });
	Component root_container = Container::Horizontal({ left_column, log_view });

	Component full_ui = Renderer(root_container, [&]() -> Element
	{
		Element connectors_panel = window(text(" Connectors  (l=launch, s=stop) "), connector_menu->Render()) | flex;
		Element tasks_panel = window(text(" Tasks "), tasks_view->Render()) | flex;
		Element log_panel = window(text(" App Log "), log_view->Render()) | flex;
		return vbox({
			text(status_line) | bold,
			hbox({
				vbox({ connectors_panel, tasks_panel }) | flex,
				log_panel | flex,
			}) | flex,
			text("q: quit   l: launch selected   s: stop selected   j/k or arrows: move") | dim,
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

		if (event == Event::q || event == Event::CtrlC)
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

	std::thread poller([&]()
	{
		while (running_flag)
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

			for (int waited_ms = 0; waited_ms < 500 && running_flag; waited_ms += 100)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
	});

	screen.Loop(full_ui);

	running_flag = false;
	if (poller.joinable())
	{
		poller.join();
	}

	return 0;
}

} // namespace metaagent::cli
