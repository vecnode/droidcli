#pragma once

#include "ai/types.hpp"
#include "export.hpp"

namespace metaagent::ai {

METAAGENT_API core::String build_ollama_chat_url(const OllamaConfig& config);

METAAGENT_API OllamaOutboundRequest build_ollama_chat_request(
	const OllamaConfig& config,
	const core::Array<ChatMessage>& messages);

METAAGENT_API OllamaChatResponse parse_ollama_chat_response(
	int32_t status_code,
	const core::String& response_body,
	bool transport_ok);

} // namespace metaagent::ai
