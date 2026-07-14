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
};

struct LanguageAiSnapshot {
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
