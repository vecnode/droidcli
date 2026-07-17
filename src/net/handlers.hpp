#pragma once

#include "export.hpp"
#include "net/types.hpp"
#include "notify/types.hpp"
#include "session/types.hpp"

namespace droidcli::ai {
class LanguageRuntime;
struct LanguageAiTransportCallbacks;
} // namespace droidcli::ai

namespace droidcli::net {

struct HandlerContext {
	session::RuntimeSession session;
	ai::LanguageRuntime* language_ai = nullptr;
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

DROIDCLI_API HttpResponse handle_health(const HandlerContext& context);

DROIDCLI_API HttpResponse handle_echo(const HttpRequest& request);

DROIDCLI_API NotifyHandleResult handle_notify(const HttpRequest& request);

DROIDCLI_API AiChatHandleResult handle_ai_chat(const HttpRequest& request, const HandlerContext& context);

} // namespace droidcli::net
