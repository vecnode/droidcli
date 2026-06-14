#pragma once

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

	int socket_handle_ = -1;
	MiniHttpServerOptions options_;
	metaagent::net::RouteTable routes_;
};

} // namespace metaagent::tools
