#pragma once

#include "ai/types.hpp"
#include "export.hpp"

// droidcli's provider seam (ZeroClaw's own Core-tier zeroclaw-providers role
// - see ARCHITECTURE.md's crate comparison; that's ZeroClaw's tier name,
// not droidcli's own module layer, where droidcli-providers lives in
// Services). One concrete provider: any backend speaking the OpenAI Chat
// Completions wire format, defaulting to a local Ollama daemon (see
// OpenAICompatConfig in ai/types.hpp). Request/response shaping lives here;
// DroidHost owns the actual POST via LanguageTransportCallbacks.
namespace droidcli::ai {

DROIDCLI_API core::String build_openai_chat_url(const OpenAICompatConfig& config);

DROIDCLI_API OpenAICompatOutboundRequest build_openai_chat_request(
	const OpenAICompatConfig& config,
	const core::Array<ChatMessage>& messages,
	const core::Array<ToolDefinition>& tools = {});

DROIDCLI_API OpenAICompatChatResponse parse_openai_chat_response(
	int32_t status_code,
	const core::String& response_body,
	bool transport_ok);

} // namespace droidcli::ai
