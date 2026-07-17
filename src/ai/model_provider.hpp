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

	// Raw "Name: value" header lines the transport should send in addition
	// to Content-Type/Content-Length - empty for OllamaProvider (no auth
	// header needed for a local peer), populated by AnthropicProvider with
	// "x-api-key"/"anthropic-version". See LanguageAiTransportCallbacks.
	core::Array<core::String> headers;
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

	// Generation telemetry, provider-agnostic units (milliseconds, token
	// counts) - a second ModelProvider that has no equivalent field just
	// leaves these at zero/empty rather than every caller special-casing
	// "provider doesn't report this". Ollama's raw nanosecond durations are
	// converted to milliseconds by OllamaProvider::parse_response; see
	// ollama_client.hpp's OllamaChatResponse for the wire-unit source.
	// Consumed by DroidHost::agent_turn for structured per-hop "ollama"
	// channel logging - see "Ollama telemetry" in ARCHITECTURE.md.
	int64_t total_duration_ms = 0;
	int64_t eval_duration_ms = 0;
	int64_t prompt_tokens = 0;
	int64_t completion_tokens = 0;
	core::String done_reason;
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

// Adapts ai::anthropic_client's free functions behind the ModelProvider
// interface - same adapter-only discipline as OllamaProvider. This is the
// second concrete ModelProvider (see "Second ModelProvider" in
// ARCHITECTURE.md); DroidHost::agent_turn selects between the two by
// constructing whichever one HostConfig::ai_provider names - nothing in
// agent_turn's own control flow changes for either.
class DROIDCLI_API AnthropicProvider : public ModelProvider {
public:
	explicit AnthropicProvider(AnthropicConfig config);

	ProviderRequest build_request(
		const core::Array<ChatMessage>& transcript,
		const core::Array<ToolDefinition>& tools) const override;

	ProviderResponse parse_response(
		int32_t status_code,
		const core::String& response_body,
		bool transport_ok) const override;

private:
	AnthropicConfig config_;
};

} // namespace droidcli::ai
