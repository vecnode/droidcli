#pragma once

#include "ai/language_runtime.hpp"
#include "ai/types.hpp"
#include "net/router.hpp"
#include "net/types.hpp"
#include "session/types.hpp"

#include <cstdint>
#include <functional>

namespace droidcli::tools {

using NotifyCallback = std::function<void(const droidcli::core::String& message)>;

// Fallback dispatch tried when the built-in net::RouteTable (health/echo/notify/
// ai-chat) doesn't handle a request. Return true and fill out_response if handled.
using CustomRouteFn = std::function<bool(
	const droidcli::net::HttpRequest& request,
	droidcli::net::HttpResponse& out_response)>;

struct MiniHttpServerOptions {
	int32_t port = 30080;
	droidcli::session::RuntimeSession session;
	NotifyCallback on_notify;
	bool enable_language_ai = true;
	droidcli::ai::OllamaConfig ollama_config;
	droidcli::core::String system_prompt;
	CustomRouteFn custom_dispatch;
};

class MiniHttpServer {
public:
	bool start(const MiniHttpServerOptions& options);
	void stop();
	bool poll_once(int32_t timeout_ms = 50);
	bool is_running() const { return socket_handle_ >= 0; }

private:
	bool read_request(int client_socket, droidcli::net::HttpRequest& out_request) const;
	bool write_response(int client_socket, const droidcli::net::HttpResponse& response) const;
	void configure_language_ai(const MiniHttpServerOptions& options);

	int socket_handle_ = -1;
	MiniHttpServerOptions options_;
	droidcli::net::RouteTable routes_;
	droidcli::ai::LanguageAiRuntime language_ai_;
	droidcli::ai::LanguageAiTransportCallbacks language_ai_transport_;
};

} // namespace droidcli::tools
