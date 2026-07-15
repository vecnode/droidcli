#pragma once

#include "ai/types.hpp"
#include "export.hpp"

namespace droidcli::ai {

// Result of shaping one outbound request for a ModelProvider. Mirrors
// OllamaOutboundRequest's shape (see ollama_client.hpp) but is provider-
// agnostic - a caller never needs to know which provider produced it.
struct ProviderRequest {
	bool valid = false;
	core::String url;
	core::String body;
	core::String error_message;
};

// Result of parsing one inbound response for a ModelProvider. Mirrors
// OllamaChatResponse's shape but drops fields (status_code, done, model)
// that agent_turn's tool-calling loop never actually reads.
struct ProviderResponse {
	bool transport_ok = false;
	bool http_success = false;
	core::String assistant_message;
	core::Array<ToolCall> tool_calls;
	core::String error_message;
};

// Everything DroidHost::agent_turn needs from an LLM backend, decoupled
// from any single provider's wire format: build a provider-specific HTTP
// request from a transcript + tool set, and parse a provider-specific HTTP
// response back into ProviderResponse. A ModelProvider never performs the
// actual network call itself - the host still owns that (via
// LanguageAiTransportCallbacks) - it only knows how to shape and interpret
// its own wire format.
//
// This is droidcli's Core-tier "providers" role (see ARCHITECTURE.md's
// crate comparison to ZeroClaw's zeroclaw-providers). Ollama is the only
// implementation today (OllamaProvider, below). Adding a second provider
// (Anthropic/OpenAI/...) means implementing this interface and having the
// host select/construct an instance - not adding a second code path to
// agent_turn.
class ModelProvider {
public:
	virtual ~ModelProvider() = default;

	virtual ProviderRequest build_request(
		const core::Array<ChatMessage>& transcript,
		const core::Array<ToolDefinition>& tools) const = 0;

	virtual ProviderResponse parse_response(
		int32_t status_code,
		const core::String& response_body,
		bool transport_ok) const = 0;
};

// Adapts ai::ollama_client's existing free functions
// (build_ollama_chat_request / parse_ollama_chat_response - the tested
// implementation, see tests/ollama_client_test.cpp) behind the
// ModelProvider interface. Purely an adapter, not a reimplementation - this
// class holds no request/response-shaping logic of its own.
class DROIDCLI_API OllamaProvider : public ModelProvider {
public:
	explicit OllamaProvider(OllamaConfig config);

	ProviderRequest build_request(
		const core::Array<ChatMessage>& transcript,
		const core::Array<ToolDefinition>& tools) const override;

	ProviderResponse parse_response(
		int32_t status_code,
		const core::String& response_body,
		bool transport_ok) const override;

private:
	OllamaConfig config_;
};

} // namespace droidcli::ai
