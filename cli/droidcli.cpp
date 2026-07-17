#include "host.hpp"
#include "http_mount.hpp"
#include "droidcli_core.h"
#include "net/connector.hpp"
#include "settings_store.hpp"
#include "tools/mini_http_server.hpp"
#include "tui.hpp"
#include "windows_service.hpp"

#include <algorithm>
#include <cstdlib>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

// Config hardening (see "Config hardening" in ARCHITECTURE.md): resolves a
// setting as CLI flag (if actually present in argv) > settings-file value
// (if non-empty) > default_value. Deliberately checks argv directly for the
// flag rather than reusing parse_string_arg's own already-defaulted return
// value - parse_string_arg("--ollama-url", "http://127.0.0.1:11434") returns
// that default whether or not the flag was actually passed, which would
// make an explicit flag indistinguishable from "not passed" here and always
// beat the settings file even when the user never typed the flag.
std::string resolve_setting_string(int argc, char** argv, const char* flag,
	const std::string& settings_value, const std::string& default_value)
{
	for (int index = 1; index < argc; ++index)
	{
		if (std::string(argv[index]) == flag && index + 1 < argc)
		{
			return argv[index + 1];
		}
	}
	if (!settings_value.empty())
	{
		return settings_value;
	}
	return default_value;
}

// Same CLI flag > settings-file > default precedence as resolve_setting_string,
// for an integer setting (currently just --ollama-num-ctx). settings_value
// of 0 is treated as "not set" the same way resolve_setting_string treats an
// empty string, since 0 is never a meaningful context-window size.
int resolve_setting_int(int argc, char** argv, const char* flag, int settings_value, int default_value)
{
	for (int index = 1; index < argc; ++index)
	{
		if (std::string(argv[index]) == flag && index + 1 < argc)
		{
			return std::atoi(argv[index + 1]);
		}
	}
	if (settings_value > 0)
	{
		return settings_value;
	}
	return default_value;
}

// 32 random bytes, hex-encoded, from std::random_device (a non-deterministic
// OS entropy source on Windows/Linux). Used as the API bearer token when the
// operator doesn't supply --token / DROIDCLI_API_TOKEN, so droidcli never
// runs its shell-execution-capable HTTP API with zero authentication.
std::string generate_random_token()
{
	std::random_device entropy;
	std::uniform_int_distribution<int> byte_distribution(0, 255);
	std::ostringstream stream;
	stream << std::hex << std::setfill('0');
	for (int i = 0; i < 32; ++i)
	{
		stream << std::setw(2) << byte_distribution(entropy);
	}
	return stream.str();
}

std::string resolve_api_token(int argc, char** argv, const std::string& settings_token)
{
	const std::string flag_value = parse_string_arg(argc, argv, "--token", "");
	if (!flag_value.empty())
	{
		return flag_value;
	}

	const char* env_value = std::getenv("DROIDCLI_API_TOKEN");
	if (env_value != nullptr && env_value[0] != '\0')
	{
		return env_value;
	}

	if (!settings_token.empty())
	{
		// Reused from a previous run's settings file - not freshly generated,
		// so no "save this" banner; the whole point of persisting it was so
		// the operator doesn't have to.
		return settings_token;
	}

	const std::string generated = generate_random_token();
	std::cout << "droidcli: generated API token (save this): " << generated << std::endl;
	std::cout << "droidcli: pass it as \"Authorization: Bearer " << generated
		<< "\" on every /api/* and /ai/chat request, or set --token/DROIDCLI_API_TOKEN "
		"next time to reuse a token across restarts." << std::endl;
	return generated;
}

// Second ai::ModelProvider (see "Second ModelProvider" in ARCHITECTURE.md) -
// mirrors resolve_api_token's flag > env var precedence, but with no
// generated fallback: an Anthropic API key can't be conjured locally the
// way a bearer token can, so an empty return here just means the operator
// hasn't configured it yet (fine as long as --provider stays "ollama").
std::string resolve_anthropic_api_key(int argc, char** argv, const std::string& settings_key)
{
	const std::string flag_value = parse_string_arg(argc, argv, "--anthropic-api-key", "");
	if (!flag_value.empty())
	{
		return flag_value;
	}

	const char* env_value = std::getenv("ANTHROPIC_API_KEY");
	if (env_value != nullptr && env_value[0] != '\0')
	{
		return env_value;
	}

	return settings_key;
}

// The absolute path to this running executable, for building the command
// line --install-service registers with the Service Control Manager (which
// needs an absolute path, not whatever relative/PATH-resolved form argv[0]
// happens to be). Falls back to argv[0] verbatim if GetModuleFileNameA is
// unavailable/fails, or on a non-Windows build.
std::string current_executable_path(char** argv)
{
#if defined(_WIN32)
	char buffer[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
	if (length > 0 && length < MAX_PATH)
	{
		return std::string(buffer, length);
	}
#endif
	return argv[0] != nullptr ? argv[0] : "droidcli";
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

// Runtime state (currently: registered connectors) persisted across
// restarts, separately from --config: --config is operator-authored and
// reloaded verbatim every run, this file is droidcli's own save of whatever
// was registered at runtime (via POST /api/connectors or the agent) so it
// isn't lost the moment the process exits. Same JSON shape as --config
// ({"connectors":[...]}), so it reuses load_connectors_from_config() -
// loaded first, with --config applied on top (register_connector replaces
// by id, so --config entries win over a stale saved state for the same id).
// db/ is created by DroidHost::initialize() (called before both
// load_state_if_present and save_state - see main()) so it always exists by
// the time this path is touched.
constexpr const char* kStateFilePath = "db/droidcli_state.json";

void load_state_if_present(droidcli::cli::DroidHost& host)
{
	std::ifstream check(kStateFilePath);
	if (!check.good())
	{
		return; // No prior state (e.g. first run) - not an error.
	}
	check.close();
	load_connectors_from_config(kStateFilePath, host);
	std::cout << "droidcli: restored saved state from " << kStateFilePath << std::endl;
}

void save_state(droidcli::cli::DroidHost& host)
{
	std::ofstream out(kStateFilePath, std::ios::trunc);
	if (!out)
	{
		std::cerr << "droidcli: could not save state to " << kStateFilePath << std::endl;
		return;
	}
	out << host.list_connectors_json();
}

void print_usage()
{
	std::cout <<
		"droidcli " << droidcli::version_string << " - a self-contained CLI agent daemon\n"
		"\n"
		"Usage: droidcli [options]\n"
		"\n"
		"Options:\n"
		"  --port <N>             HTTP listen port (default 30080)\n"
		"  --config <path>        JSON file with a top-level \"connectors\" array, loaded at startup\n"
		"  --settings <path>      JSON settings file (default db/droidcli_settings.json) - port/ollama/\n"
		"                          provider config and secrets (token, Anthropic key, DPAPI-encrypted at\n"
		"                          rest on Windows). CLI flags override it; written at every startup so a\n"
		"                          later --install-service run has a token to read with none on its command line.\n"
		"  --token <value>        Bearer token for the HTTP API (else DROIDCLI_API_TOKEN env var, else generated)\n"
		"  --no-ai                 Disable Ollama / /ai/chat and the agent tool-calling loop\n"
		"  --enable-hardware-scan  Opt in to a one-time local CPU/GPU/RAM/disk inventory scan at startup\n"
		"  --ollama-url <url>      Ollama base URL (default http://127.0.0.1:11434)\n"
		"  --ollama-model <name>   Ollama model name (default llama3.2)\n"
		"  --ollama-num-ctx <N>    Ollama context window, in tokens, requested on every chat call\n"
		"                          (default 32768, matching OpenClaude - prevents Ollama's own,\n"
		"                          often smaller, per-model default from silently truncating a\n"
		"                          long agent-turn transcript)\n"
		"  --provider <name>       Chat model provider: \"ollama\" (default) or \"anthropic\"\n"
		"  --anthropic-api-key <k> Anthropic API key (else ANTHROPIC_API_KEY env var) - required if --provider anthropic\n"
		"  --anthropic-model <n>   Anthropic model name (default claude-3-5-haiku-latest)\n"
		"  --headless              Skip the interactive TUI; run the plain foreground daemon loop only\n"
		"  --daemon                Documented no-op - use a process supervisor for background operation\n"
		"  --install-service       (Windows, Administrator) Register droidcli as a Windows Service\n"
		"  --uninstall-service     (Windows, Administrator) Unregister the droidcli Windows Service\n"
		"  --service               Internal - used by the Service Control Manager to run the installed\n"
		"                          service; do not pass this by hand from a console\n"
		"  --help, -h              Show this help and exit\n"
		"  --version, -v           Show the version and exit\n"
		"\n"
		"Runtime-registered connectors are saved to " << kStateFilePath << " on exit and\n"
		"restored on the next start; the durable session log is written to logs/log.txt.\n"
		<< std::endl;
}

} // namespace

int main(int argc, char** argv)
{
	if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h"))
	{
		print_usage();
		return 0;
	}
	if (has_flag(argc, argv, "--version") || has_flag(argc, argv, "-v"))
	{
		std::cout << "droidcli " << droidcli::version_string << std::endl;
		return 0;
	}

	droidcli::initialize_defaults();

	// Config hardening (see "Config hardening" in ARCHITECTURE.md): load
	// whatever was persisted last run as the fallback layer between CLI
	// flags/env vars and hardcoded defaults. Absent/unparsable is fine -
	// `settings` just stays at its own struct defaults, same as
	// db/droidcli_state.json's "absent is fine" convention.
	const std::string settings_path = parse_string_arg(argc, argv, "--settings", "db/droidcli_settings.json");
	droidcli::cli::HostSettings settings;
	droidcli::cli::load_settings(settings_path, settings);

	const int port = parse_port(argc, argv, settings.port);
	const bool enable_ai = !has_flag(argc, argv, "--no-ai");
	const bool enable_hardware_scan = has_flag(argc, argv, "--enable-hardware-scan") || settings.enable_hardware_scan;
	const std::string ollama_url = resolve_setting_string(argc, argv, "--ollama-url", settings.ollama_url, "http://127.0.0.1:11434");
	const std::string ollama_model = resolve_setting_string(argc, argv, "--ollama-model", settings.ollama_model, "llama3.2");
	const int ollama_num_ctx = resolve_setting_int(argc, argv, "--ollama-num-ctx", settings.ollama_num_ctx, 32768);
	const std::string ai_provider = resolve_setting_string(argc, argv, "--provider", settings.ai_provider, "ollama");
	const std::string anthropic_api_key = resolve_anthropic_api_key(argc, argv, settings.anthropic_api_key);
	const std::string anthropic_model = resolve_setting_string(argc, argv, "--anthropic-model", settings.anthropic_model, "claude-3-5-haiku-latest");
	const std::string config_path = parse_string_arg(argc, argv, "--config", "");
	const std::string api_token = resolve_api_token(argc, argv, settings.api_token);

	// Persist the resolved values immediately (not just on clean exit, like
	// db/droidcli_state.json's connectors) - a later `--install-service` run
	// needs this file to already have a real token in it, and shouldn't
	// depend on this process having shut down cleanly first.
	droidcli::cli::HostSettings resolved_settings;
	resolved_settings.port = port;
	resolved_settings.enable_ai = enable_ai;
	resolved_settings.enable_hardware_scan = enable_hardware_scan;
	resolved_settings.ollama_url = ollama_url;
	resolved_settings.ollama_model = ollama_model;
	resolved_settings.ollama_num_ctx = ollama_num_ctx;
	resolved_settings.ai_provider = ai_provider;
	resolved_settings.anthropic_model = anthropic_model;
	resolved_settings.api_token = api_token;
	resolved_settings.anthropic_api_key = anthropic_api_key;
	droidcli::cli::save_settings(settings_path, resolved_settings);

	if (ollama_num_ctx <= 0)
	{
		std::cerr << "droidcli: --ollama-num-ctx must be a positive integer (got " << ollama_num_ctx << ")" << std::endl;
		return 1;
	}
	if (ai_provider != "ollama" && ai_provider != "anthropic")
	{
		std::cerr << "droidcli: --provider must be \"ollama\" or \"anthropic\" (got \"" << ai_provider << "\")" << std::endl;
		return 1;
	}
	if (ai_provider == "anthropic" && anthropic_api_key.empty())
	{
		std::cerr << "droidcli: --provider anthropic requires --anthropic-api-key or ANTHROPIC_API_KEY" << std::endl;
		return 1;
	}

	// Background service (see "Real background service" in ARCHITECTURE.md):
	// --install-service/--uninstall-service are one-shot Service Control
	// Manager actions, not "run droidcli" - handled here, before any
	// host/HTTP-server setup, and always exit immediately after. The
	// settings file was already (re)written above with everything this
	// invocation's flags resolved to, so the installed service's own command
	// line only needs --service/--headless/--settings - no --token or
	// --anthropic-api-key on a command line any process on the machine can
	// read.
	if (has_flag(argc, argv, "--uninstall-service"))
	{
		const bool ok = droidcli::cli::uninstall_windows_service();
		return ok ? 0 : 1;
	}
	if (has_flag(argc, argv, "--install-service"))
	{
		const std::string command_line = "\"" + current_executable_path(argv)
			+ "\" --service --headless --settings \"" + settings_path + "\"";
		const bool ok = droidcli::cli::install_windows_service(command_line);
		if (ok)
		{
			std::cout << "droidcli: service \"" << droidcli::cli::kWindowsServiceName
				<< "\" installed. Start it with: sc start " << droidcli::cli::kWindowsServiceName
				<< " (or via the Services console) - requires an elevated (Administrator) shell." << std::endl;
		}
		return ok ? 0 : 1;
	}

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
	host_config.enable_hardware_scan = enable_hardware_scan;
	host_config.ollama_url = ollama_url;
	host_config.ollama_model = ollama_model;
	host_config.ollama_num_ctx = ollama_num_ctx;
	host_config.ai_provider = ai_provider;
	host_config.anthropic_api_key = anthropic_api_key;
	host_config.anthropic_model = anthropic_model;
	// So a runtime model/provider change (POST /api/config, POST
	// /api/ollama/config, the TUI's model picker) persists across restarts
	// the same way a --ollama-model flag already does - see "Model/provider
	// changes persist at runtime too" in ARCHITECTURE.md.
	host_config.settings_path = settings_path;

	host.configure(host_config);
	host.initialize();

	load_state_if_present(host);
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
	options.api_token = api_token;

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
	std::cout << "  GET  /health                          (no auth required)" << std::endl;
	std::cout << "  GET  /api/status                       [Bearer token required]" << std::endl;
	std::cout << "  GET  /api/connectors   POST /api/connectors                     [Bearer token required]" << std::endl;
	std::cout << "  GET  /api/connectors/{id}/status   POST .../launch  .../stop  .../call   [Bearer token required]" << std::endl;
	std::cout << "  GET  /api/tasks   POST /api/tasks   GET /api/tasks/{id}   POST /api/tasks/{id}/cancel   [Bearer token required]" << std::endl;
	std::cout << "  POST /api/run                          [Bearer token required]" << std::endl;
	std::cout << "  POST /api/open   POST /api/apps/find   GET /api/apps/open    [Bearer token required]" << std::endl;
	std::cout << "  POST /api/fs/read  /api/fs/write  /api/fs/list  /api/fs/stat  /api/fs/which  GET /api/fs/cwd   [Bearer token required]" << std::endl;
	std::cout << "  POST /api/fs/copy  /api/fs/move  /api/fs/delete  /api/fs/mkdir       [Bearer token required]" << std::endl;
	std::cout << "  GET/POST /api/locations                 [Bearer token required]" << std::endl;
	std::cout << "  GET/POST /api/clipboard                 [Bearer token required]" << std::endl;
	std::cout << "  POST /api/agent/turn                   [Bearer token required]" << std::endl;
	std::cout << "  GET  /api/agent/self_status             [Bearer token required]" << std::endl;
	std::cout << "  GET  /api/hardware                     [Bearer token required] (requires --enable-hardware-scan)" << std::endl;
	std::cout << "  GET  /api/ollama/setup-status          [Bearer token required]" << std::endl;
	std::cout << "  POST /api/ollama/install   POST /api/ollama/start   POST /api/ollama/pull   [Bearer token required]" << std::endl;
	if (enable_ai)
	{
		std::cout << "  POST /ai/chat  (Ollama: " << ollama_url << ", model: " << ollama_model
			<< ")   [Bearer token required]" << std::endl;
	}

	if (has_flag(argc, argv, "--service"))
	{
		// Internal entry point - the Service Control Manager launches an
		// installed service with exactly this flag (see --install-service
		// above). Never falls through to the headless/TUI branches below:
		// a service invocation that isn't actually running under the SCM
		// (e.g. --service typed by hand from a console) should fail loudly,
		// not silently degrade into an interactive-looking foreground run.
		std::cout << "droidcli: starting as a Windows Service (waiting for the Service Control Manager)..." << std::endl;
		const bool service_ok = droidcli::cli::run_as_windows_service(
			[&](volatile bool& service_running)
			{
				// Same defense-in-depth shape as the --headless loop below -
				// an uncaught exception from one poll/tick iteration must
				// never take the whole service down.
				while (service_running)
				{
					try
					{
						if (!server.poll_once(200))
						{
							break;
						}
						host.tick(0.2f);
					}
					catch (const std::exception& e)
					{
						std::cerr << "droidcli: service loop error: " << e.what() << std::endl;
					}
					catch (...)
					{
						std::cerr << "droidcli: service loop error: unknown exception" << std::endl;
					}
				}
			},
			[&]()
			{
				save_state(host);
				server.stop();
			});

		if (!service_ok)
		{
			std::cerr << "droidcli: --service was passed but this process was not launched by the "
				"Windows Service Control Manager. Install it first with --install-service, then "
				"start it via \"sc start " << droidcli::cli::kWindowsServiceName
				<< "\" or the Services console - do not pass --service by hand." << std::endl;
			return 1;
		}
		std::cout << "droidcli: service stopped." << std::endl;
		return 0;
	}

	const bool headless = has_flag(argc, argv, "--headless");

	if (headless)
	{
		// Unchanged from before the TUI existed: foreground daemon loop only,
		// no terminal UI. Keeps droidcli scriptable for CI/systemd use.
		//
		// Defense in depth (mirrors the TUI's own try/catch, see cli/tui.cpp):
		// an uncaught exception from a poll_once/tick iteration should never
		// abort the whole daemon - log it and keep looping.
		while (g_running)
		{
			try
			{
				if (!server.poll_once(200))
				{
					break;
				}
				host.tick(0.2f);
			}
			catch (const std::exception& e)
			{
				std::cerr << "droidcli: headless loop error: " << e.what() << std::endl;
			}
			catch (...)
			{
				std::cerr << "droidcli: headless loop error: unknown exception" << std::endl;
			}
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
				try
				{
					if (!server.poll_once(200))
					{
						break;
					}
					host.tick(0.2f);
				}
				catch (const std::exception& e)
				{
					std::cerr << "droidcli: HTTP server loop error: " << e.what() << std::endl;
				}
				catch (...)
				{
					std::cerr << "droidcli: HTTP server loop error: unknown exception" << std::endl;
				}
			}
		});

		// run_tui() already wraps its own event loop and chat-submit handler in
		// try/catch (see cli/tui.cpp) - this is one more layer of defense in
		// depth per the "network failure calling Ollama quits the terminal"
		// investigation, so a crash anywhere in the TUI path can never take the
		// whole process down without at least a chance to log why first.
		try
		{
			droidcli::cli::run_tui(host, port, g_running);
		}
		catch (const std::exception& e)
		{
			std::cerr << "droidcli: TUI terminated with an exception: " << e.what() << std::endl;
		}
		catch (...)
		{
			std::cerr << "droidcli: TUI terminated with an unknown exception." << std::endl;
		}

		g_running = false;
		if (server_thread.joinable())
		{
			server_thread.join();
		}
	}

	save_state(host);
	server.stop();
	std::cout << "droidcli stopped." << std::endl;
	return 0;
}
