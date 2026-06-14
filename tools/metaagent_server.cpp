#include "metaagent.h"
#include "tools/mini_http_server.hpp"

#include <algorithm>
#include <csignal>
#include <iostream>
#include <string>

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

} // namespace

int main(int argc, char** argv)
{
	metaagent::initialize_defaults();

	const int port = parse_port(argc, argv, 30080);
	const bool enable_ai = !has_flag(argc, argv, "--no-ai");
	const std::string ollama_url = parse_string_arg(argc, argv, "--ollama-url", "http://127.0.0.1:11434");
	const std::string ollama_model = parse_string_arg(argc, argv, "--ollama-model", "llama3.2");
	const std::string system_prompt = parse_string_arg(
		argc,
		argv,
		"--system-prompt",
		"You are a concise assistant embedded in metaagent.");

	metaagent::tools::MiniHttpServer server;
	metaagent::tools::MiniHttpServerOptions options;
	options.port = port;
	options.session.active = true;
	options.session.map_name = "metaagent_server";
	options.session.build_label = "standalone";
	options.session.http_enabled = true;
	options.session.http_router_bound = true;
	options.enable_language_ai = enable_ai;
	options.ollama_config.base_url = ollama_url;
	options.ollama_config.model = ollama_model;
	options.ollama_config.enabled = enable_ai;
	options.system_prompt = system_prompt;
	options.on_notify = [](const metaagent::core::String& message)
	{
		std::cout << "[notify] " << message << std::endl;
	};

	if (!server.start(options))
	{
		std::cerr << "Failed to bind HTTP server on port " << port << std::endl;
		return 1;
	}

	std::signal(SIGINT, handle_signal);
#if !defined(_WIN32)
	std::signal(SIGTERM, handle_signal);
#endif

	std::cout << "metaagent_server listening on http://127.0.0.1:" << port << std::endl;
	std::cout << "  GET  /health" << std::endl;
	std::cout << "  GET  /echo?msg=hello" << std::endl;
	std::cout << "  POST /notify" << std::endl;
	if (enable_ai)
	{
		std::cout << "  POST /ai/chat  (Ollama: " << ollama_url << ", model: " << ollama_model << ")" << std::endl;
	}

	while (g_running)
	{
		if (!server.poll_once(200))
		{
			break;
		}
	}

	server.stop();
	std::cout << "metaagent_server stopped." << std::endl;
	return 0;
}
