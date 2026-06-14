#pragma once

#include "ai/language_runtime.hpp"
#include "ai/types.hpp"
#include "net/router.hpp"
#include "net/types.hpp"
#include "session/types.hpp"

#include <cstdint>
#include <functional>

namespace metaagent::tools {

using NotifyCallback = std::function<void(const metaagent::core::String& message)>;

struct MiniHttpServerOptions {
	int32_t port = 30080;
	metaagent::session::RuntimeSession session;
	NotifyCallback on_notify;
	bool enable_language_ai = true;
	metaagent::ai::OllamaConfig ollama_config;
	metaagent::core::String system_prompt;
};

class MiniHttpServer {
public:
	bool start(const MiniHttpServerOptions& options);
	void stop();
	bool poll_once(int32_t timeout_ms = 50);
	bool is_running() const { return socket_handle_ >= 0; }

private:
	bool read_request(int client_socket, metaagent::net::HttpRequest& out_request) const;
	bool write_response(int client_socket, const metaagent::net::HttpResponse& response) const;
	void configure_language_ai(const MiniHttpServerOptions& options);

	int socket_handle_ = -1;
	MiniHttpServerOptions options_;
	metaagent::net::RouteTable routes_;
	metaagent::ai::LanguageAiRuntime language_ai_;
	metaagent::ai::LanguageAiTransportCallbacks language_ai_transport_;
};

} // namespace metaagent::tools
