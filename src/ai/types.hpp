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

// droidcli's single LLM backend config: any provider speaking the OpenAI
// Chat Completions wire format (POST {base_url}/chat/completions). Defaults
// to a local Ollama daemon, which exposes that exact format at its built-in
// `/v1` endpoint - no Ollama-specific request shaping lives here, so
// pointing base_url at a different OpenAI-compatible host (LM Studio, vLLM,
// a real OpenAI-style gateway) works without any code change. See
// "The LLM provider" in ARCHITECTURE.md.
struct OpenAICompatConfig {
	core::String base_url = "http://127.0.0.1:11434";
	core::String model = "llama3.2";
	bool enabled = true;
	bool stream = false;
	float temperature = -1.0f;
	// Context window size, in tokens, requested via Ollama's own "options"
	// extension on every chat call - not left at Ollama's own (often small)
	// per-model default, which can silently truncate a long agent-turn
	// transcript. 32768 matches OpenClaude's own default (see "Context
	// window (num_ctx)" in ARCHITECTURE.md's OpenClaude comparison) -
	// overridable via HostConfig::ollama_num_ctx / --ollama-num-ctx, not a
	// fixed constant. Harmless to send to a strict (non-Ollama)
	// OpenAI-compatible backend - it's a sibling top-level field, not
	// nested inside anything the standard schema validates.
	int32_t num_ctx = 32768;
};

struct OpenAICompatOutboundRequest {
	bool valid = false;
	core::String url;
	core::String body;
	core::String error_message;
};

struct OpenAICompatChatResponse {
	bool transport_ok = false;
	bool http_success = false;
	int32_t status_code = 0;
	bool done = false;
	core::String model;
	core::String assistant_message;
	// The model's chain-of-thought, when the backend streams one back
	// alongside the answer - scraped from whichever of "reasoning_content"
	// (DeepSeek/GLM-style) or "reasoning" (Ollama's own naming, confirmed
	// against a real `glm-4.7-flash` response) the message object actually
	// contains. Never treated as the assistant's real reply - see
	// "Thinking is observability, not narration" in ARCHITECTURE.md; the
	// caller logs it, it never enters agent_transcript_.
	core::String thinking_text;
	core::Array<ToolCall> tool_calls;
	core::String error_message;

	// Generation telemetry, OpenAI's own wire field names/units (token
	// counts only - this wire format reports no per-call duration, unlike
	// Ollama's native /api/chat). Zero when absent. Consumed by
	// ModelProvider::parse_response to feed structured per-hop logging in
	// DroidHost::agent_turn - see "Ollama telemetry" in ARCHITECTURE.md.
	int64_t prompt_tokens = 0;
	int64_t completion_tokens = 0;
	core::String done_reason;
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
