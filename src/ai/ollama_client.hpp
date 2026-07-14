#pragma once

#include "ai/types.hpp"
#include "export.hpp"

namespace droidcli::ai {

DROIDCLI_API core::String build_ollama_chat_url(const OllamaConfig& config);

DROIDCLI_API OllamaOutboundRequest build_ollama_chat_request(
	const OllamaConfig& config,
	const core::Array<ChatMessage>& messages);

DROIDCLI_API OllamaChatResponse parse_ollama_chat_response(
	int32_t status_code,
	const core::String& response_body,
	bool transport_ok);

} // namespace droidcli::ai
