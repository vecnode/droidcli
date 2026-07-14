#include "host.hpp"
#include "http_mount.hpp"
#include "droidcli.h"
#include "net/connector.hpp"
#include "tools/mini_http_server.hpp"
#include "tui.hpp"

#include <algorithm>
#include <cstdlib>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile bool g_running = true;

void handle_signal(int)
{
	g_running = false;
}

int parse_port(int argc, char** argv, int default_port)
{
	for (int index = 1; index < argc; ++index)
	{
		const std::string arg = argv[index];
		if (arg == "--port" && index + 1 < argc)
		{
			return std::max(1, std::atoi(argv[index + 1]));
		}
	}
	return default_port;
}

std::string parse_string_arg(int argc, char** argv, const char* flag, const char* default_value)
{
	for (int index = 1; index < argc; ++index)
	{
		const std::string arg = argv[index];
		if (arg == flag && index + 1 < argc)
		{
			return argv[index + 1];
		}
	}
	return default_value;
}

bool has_flag(int argc, char** argv, const char* flag)
{
	for (int index = 1; index < argc; ++index)
	{
		if (std::string(argv[index]) == flag)
		{
			return true;
		}
	}
	return false;
}

std::string read_text_file(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		return {};
	}
	std::ostringstream stream;
	stream << file.rdbuf();
	return stream.str();
}

// Extracts each top-level object of the "connectors" array in a config file's
// JSON body, e.g. {"connectors":[{...},{...}]}. Hand-rolled brace-depth walk,
// consistent with the rest of net/json.hpp - no JSON library dependency.
std::vector<droidcli::core::String> extract_connector_objects(const droidcli::core::String& json)
{
	std::vector<droidcli::core::String> objects;
	const size_t array_key = json.find("\"connectors\":");
	if (array_key == droidcli::core::String::npos)
	{
		return objects;
	}
	size_t cursor = json.find('[', array_key);
	if (cursor == droidcli::core::String::npos)
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

void load_connectors_from_config(const std::string& path, droidcli::cli::DroidHost& host)
{
	const std::string content = read_text_file(path);
	if (content.empty())
	{
		std::cerr << "droidcli: could not read config file: " << path << std::endl;
		return;
	}

	const auto objects = extract_connector_objects(content);
	for (const droidcli::core::String& object_json : objects)
	{
		droidcli::net::Connector connector;
		droidcli::core::String error;
		if (droidcli::net::parse_connector_from_json(object_json, connector, error))
		{
			host.register_connector(object_json);
			std::cout << "droidcli: loaded connector \"" << connector.id << "\" (" << connector.kind << ")" << std::endl;
		}
		else
		{
			std::cerr << "droidcli: skipping invalid connector entry: " << error << std::endl;
		}
	}
}

} // namespace

int main(int argc, char** argv)
{
	droidcli::initialize_defaults();

	const int port = parse_port(argc, argv, 30080);
	const bool enable_ai = !has_flag(argc, argv, "--no-ai");
	const std::string ollama_url = parse_string_arg(argc, argv, "--ollama-url", "http://127.0.0.1:11434");
	const std::string ollama_model = parse_string_arg(argc, argv, "--ollama-model", "llama3.2");
	const std::string config_path = parse_string_arg(argc, argv, "--config", "");

	if (has_flag(argc, argv, "--daemon"))
	{
		// Background-detach is not implemented on Windows (no trivial POSIX-style
		// double-fork equivalent without a service wrapper) - documented as a
		// deviation from the plan rather than over-engineered. Runs in the
		// foreground either way.
		std::cout << "droidcli: --daemon requested but background-detach is not implemented; "
			"running in the foreground. Use a process supervisor (nssm, Task Scheduler, "
			"systemd, etc.) to run this as a background service." << std::endl;
	}

	droidcli::cli::DroidHost host;
	droidcli::cli::HostConfig host_config;
	host_config.enable_ai = enable_ai;
	host_config.ollama_url = ollama_url;
	host_config.ollama_model = ollama_model;

	const char* env_google_key = std::getenv("DROIDCLI_GOOGLE_API_KEY");
	const char* env_google_cse = std::getenv("DROIDCLI_GOOGLE_CSE_ID");
	const char* env_google_query = std::getenv("DROIDCLI_GOOGLE_SEARCH_QUERY");
	if (env_google_key != nullptr) host_config.google_api_key = env_google_key;
	if (env_google_cse != nullptr) host_config.google_search_engine_id = env_google_cse;
	if (env_google_query != nullptr) host_config.google_search_query = env_google_query;

	host.configure(host_config);
	host.initialize();

	if (!config_path.empty())
	{
		load_connectors_from_config(config_path, host);
	}

	droidcli::tools::MiniHttpServer server;
	droidcli::tools::MiniHttpServerOptions options;
	options.port = port;
	options.session = host.session();
	options.enable_language_ai = enable_ai;
	options.ollama_config.base_url = ollama_url;
	options.ollama_config.model = ollama_model;
	options.ollama_config.enabled = enable_ai;
	options.on_notify = [](const droidcli::core::String& message)
	{
		std::cout << "[notify] " << message << std::endl;
	};
	options.custom_dispatch = droidcli::cli::make_droidcli_route_dispatch(host);

	if (!server.start(options))
	{
		std::cerr << "Failed to bind HTTP server on port " << port << std::endl;
		return 1;
	}

	std::signal(SIGINT, handle_signal);
#if !defined(_WIN32)
	std::signal(SIGTERM, handle_signal);
#endif

	std::cout << "droidcli listening on http://127.0.0.1:" << port << std::endl;
	std::cout << "  GET  /health" << std::endl;
	std::cout << "  GET  /api/status" << std::endl;
	std::cout << "  GET  /api/connectors   POST /api/connectors" << std::endl;
	std::cout << "  GET  /api/connectors/{id}/status   POST .../launch  .../stop  .../call" << std::endl;
	std::cout << "  GET  /api/tasks   POST /api/tasks   GET /api/tasks/{id}" << std::endl;
	if (enable_ai)
	{
		std::cout << "  POST /ai/chat  (Ollama: " << ollama_url << ", model: " << ollama_model << ")" << std::endl;
	}

	const bool headless = has_flag(argc, argv, "--headless");

	if (headless)
	{
		// Unchanged from before the TUI existed: foreground daemon loop only,
		// no terminal UI. Keeps droidcli scriptable for CI/systemd use.
		while (g_running)
		{
			if (!server.poll_once(200))
			{
				break;
			}
			host.tick(0.2f);
		}
	}
	else
	{
		// Default mode: keep serving the HTTP API on a background thread while
		// the FTXUI dashboard drives the terminal on the main thread.
		std::cout << "droidcli: starting interactive TUI (pass --headless to skip it)" << std::endl;
		std::thread server_thread([&]()
		{
			while (g_running)
			{
				if (!server.poll_once(200))
				{
					break;
				}
				host.tick(0.2f);
			}
		});

		droidcli::cli::run_tui(host, port, g_running);

		g_running = false;
		if (server_thread.joinable())
		{
			server_thread.join();
		}
	}

	server.stop();
	std::cout << "droidcli stopped." << std::endl;
	return 0;
}
