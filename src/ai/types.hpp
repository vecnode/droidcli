#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::ai {

enum class ChatRole {
	System,
	User,
	Assistant,
	Tool,
};

struct ChatMessage {
	ChatRole role = ChatRole::User;
	core::String content;
};

// A tool the model may call. `parameters_json_schema` is a raw, pre-built
// JSON Schema object (e.g. {"type":"object","properties":{...}}) rather than
// a modeled C++ type - keeps this file's hand-rolled-JSON style and avoids
// building a JSON Schema type system for one call site.
struct ToolDefinition {
	core::String name;
	core::String description;
	core::String parameters_json_schema;
};

// One function call the model asked for in a response. `arguments_json` is
// the raw JSON text of the arguments object exactly as Ollama sent it (Ollama
// sends `arguments` as a JSON object, not an OpenAI-style JSON-encoded
// string) - the caller re-parses whichever fields it needs.
struct ToolCall {
	core::String id;
	core::String name;
	core::String arguments_json;
};

struct OllamaConfig {
	core::String base_url = "http://127.0.0.1:11434";
	core::String model = "llama3.2";
	bool enabled = true;
	bool stream = false;
	float temperature = -1.0f;
	// Context window size, in tokens, requested on every chat call - not
	// left at Ollama's own (often small) per-model default, which can
	// silently truncate a long agent-turn transcript. 32768 matches
	// OpenClaude's own default (see "Context window (num_ctx)" in
	// ARCHITECTURE.md's OpenClaude comparison) - overridable via
	// HostConfig::ollama_num_ctx / --ollama-num-ctx, not a fixed constant.
	int32_t num_ctx = 32768;
};

struct OllamaOutboundRequest {
	bool valid = false;
	core::String url;
	core::String body;
	core::String error_message;
};

struct OllamaChatResponse {
	bool transport_ok = false;
	bool http_success = false;
	int32_t status_code = 0;
	bool done = false;
	core::String model;
	core::String assistant_message;
	core::Array<ToolCall> tool_calls;
	core::String error_message;

	// Ollama's own generation telemetry, verbatim wire units (nanoseconds,
	// token counts) - see parse_ollama_chat_response for the field names on
	// the wire. Zero when the field was absent (older Ollama versions, or a
	// non-2xx response that never reached this far). Consumed by
	// ModelProvider::parse_response to feed structured per-hop logging in
	// DroidHost::agent_turn - see "Ollama telemetry" in ARCHITECTURE.md.
	int64_t total_duration_ns = 0;
	int64_t eval_duration_ns = 0;
	int64_t prompt_eval_count = 0;
	int64_t eval_count = 0;
	core::String done_reason;
};

// Second ai::ModelProvider (see "Second ModelProvider" in ARCHITECTURE.md) -
// the Anthropic Messages API (https://api.anthropic.com/v1/messages).
// Mirrors OllamaConfig's shape; api_key is required (unlike Ollama, which
// needs no auth for a local peer).
struct AnthropicConfig {
	core::String base_url = "https://api.anthropic.com";
	core::String api_key;
	core::String model = "claude-3-5-haiku-latest";
	core::String api_version = "2023-06-01";
	bool enabled = true;
	int32_t max_tokens = 4096;
};

// Mirrors OllamaOutboundRequest but adds `headers` - the Messages API
// authenticates via an "x-api-key" header (plus a required
// "anthropic-version" header), unlike Ollama's unauthenticated local peer.
struct AnthropicOutboundRequest {
	bool valid = false;
	core::String url;
	core::String body;
	core::Array<core::String> headers;
	core::String error_message;
};

struct AnthropicChatResponse {
	bool transport_ok = false;
	bool http_success = false;
	int32_t status_code = 0;
	core::String model;
	core::String assistant_message;
	core::Array<ToolCall> tool_calls;
	core::String error_message;

	// Generation telemetry, verbatim wire units - see
	// parse_anthropic_messages_response for the field names on the wire.
	// Mirrors OllamaChatResponse's telemetry fields' role (Phase 30) but
	// under Anthropic's own field names (stop_reason, usage.input_tokens/
	// output_tokens) - no per-call duration is reported on this wire format,
	// unlike Ollama's total_duration/eval_duration.
	core::String stop_reason;
	int64_t input_tokens = 0;
	int64_t output_tokens = 0;
};

struct LanguageSnapshot {
	bool runtime_enabled = false;
	bool connected = false;
	bool awaiting_response = false;
	core::String model;
	core::String status_text;
	core::String last_user_message;
	core::String last_assistant_message;
	core::String representation_text;
	int32_t transcript_length = 0;
};

DROIDCLI_API core::String chat_role_to_string(ChatRole role);

DROIDCLI_API ChatRole chat_role_from_string(const core::String& role);

} // namespace droidcli::ai
