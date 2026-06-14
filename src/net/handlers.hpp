#pragma once

#include "export.hpp"
#include "net/types.hpp"
#include "notify/types.hpp"
#include "session/types.hpp"

namespace metaagent::ai {
class LanguageAiRuntime;
struct LanguageAiTransportCallbacks;
} // namespace metaagent::ai

namespace metaagent::net {

struct HandlerContext {
	session::RuntimeSession session;
	ai::LanguageAiRuntime* language_ai = nullptr;
	const ai::LanguageAiTransportCallbacks* language_ai_transport = nullptr;
};

struct NotifyHandleResult {
	HttpResponse response;
	notify::NotifyMessage notify_message;
	bool has_notify_message = false;
};

struct AiChatHandleResult {
	HttpResponse response;
	bool completed_turn = false;
};

METAAGENT_API HttpResponse handle_health(const HandlerContext& context);

METAAGENT_API HttpResponse handle_echo(const HttpRequest& request);

METAAGENT_API NotifyHandleResult handle_notify(const HttpRequest& request);

METAAGENT_API AiChatHandleResult handle_ai_chat(const HttpRequest& request, const HandlerContext& context);

} // namespace metaagent::net
