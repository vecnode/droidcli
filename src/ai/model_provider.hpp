#pragma once

#include "ai/types.hpp"
#include "export.hpp"

namespace droidcli::ai {

// Result of shaping one outbound request for a ModelProvider. Mirrors
// OpenAICompatOutboundRequest's shape (see openai_compat_client.hpp) but is
// provider-agnostic - a caller never needs to know which provider produced
// it.
struct ProviderRequest {
	bool valid = false;
	core::String url;
	core::String body;
	core::String error_message;

	// Raw "Name: value" header lines the transport should send in addition
	// to Content-Type/Content-Length. Empty for the local-Ollama default
	// (no auth header needed for a local peer); a future OpenAI-compatible
	// backend that requires an "Authorization: Bearer ..." header would
	// populate this from its own config. See LanguageTransportCallbacks.
	core::Array<core::String> headers;
};

// Result of parsing one inbound response for a ModelProvider. Mirrors
// OpenAICompatChatResponse's shape but drops fields (status_code, done,
// model) that agent_turn's tool-calling loop never actually reads.
struct ProviderResponse {
	bool transport_ok = false;
	bool http_success = false;
	core::String assistant_message;
	// The model's chain-of-thought, when the provider returns one - see
	// OpenAICompatChatResponse::thinking_text. Logged by DroidHost under
	// its own "thinking" channel, never appended to agent_transcript_ or
	// treated as the assistant's actual reply - see "Thinking is
	// observability, not narration" in ARCHITECTURE.md.
	core::String thinking_text;
	core::Array<ToolCall> tool_calls;
	core::String error_message;

	// Generation telemetry, provider-agnostic units (token counts; no
	// per-call duration - the OpenAI Chat Completions wire format doesn't
	// report one). Consumed by DroidHost::agent_turn for structured
	// per-hop "ollama" channel logging - see "Ollama telemetry" in
	// ARCHITECTURE.md.
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
// LanguageTransportCallbacks) - it only knows how to shape and interpret
// its own wire format.
//
// This is droidcli's "providers" role in the ZeroClaw crate comparison -
// ZeroClaw calls this its Core tier (see ARCHITECTURE.md's crate comparison
// to ZeroClaw's zeroclaw-providers); don't confuse that with droidcli's own
// module-layer diagram, where droidcli-providers sits in the Services
// layer, not Agent. One concrete implementation today
// (OpenAICompatProvider, below), covering any backend that speaks the
// OpenAI Chat Completions wire format - see "The LLM provider" in
// ARCHITECTURE.md for why droidcli deliberately doesn't maintain a second,
// parallel provider implementation.
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

// Adapts ai::openai_compat_client's existing free functions
// (build_openai_chat_request / parse_openai_chat_response - the tested
// implementation, see tests/openai_compat_client_test.cpp) behind the
// ModelProvider interface. Purely an adapter, not a reimplementation - this
// class holds no request/response-shaping logic of its own.
class DROIDCLI_API OpenAICompatProvider : public ModelProvider {
public:
	explicit OpenAICompatProvider(OpenAICompatConfig config);

	ProviderRequest build_request(
		const core::Array<ChatMessage>& transcript,
		const core::Array<ToolDefinition>& tools) const override;

	ProviderResponse parse_response(
		int32_t status_code,
		const core::String& response_body,
		bool transport_ok) const override;

private:
	OpenAICompatConfig config_;
};

} // namespace droidcli::ai
