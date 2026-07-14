#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::ai {

enum class ChatRole {
	System,
	User,
	Assistant,
};

struct ChatMessage {
	ChatRole role = ChatRole::User;
	core::String content;
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
