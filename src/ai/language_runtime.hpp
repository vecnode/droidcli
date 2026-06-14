#pragma once

#include "ai/ollama_client.hpp"
#include "export.hpp"

#include <functional>

namespace metaagent::ai {

struct LanguageAiTransportCallbacks {
	std::function<bool(const core::String& url, const core::String& body, int32_t& status_code_out, core::String& response_body_out)> post_json;
};

class LanguageAiRuntime {
public:
	void set_ollama_config(const OllamaConfig& config);
	const OllamaConfig& ollama_config() const { return ollama_config_; }

	void set_runtime_enabled(bool enabled) { runtime_enabled_ = enabled; }
	bool runtime_enabled() const { return runtime_enabled_; }

	void clear_transcript();
	void set_system_prompt(const core::String& prompt);
	bool submit_user_message(const core::String& message);

	OllamaOutboundRequest build_pending_chat_request() const;
	bool apply_chat_response(const OllamaChatResponse& response);
	bool complete_turn(const LanguageAiTransportCallbacks& transport);

	const core::Array<ChatMessage>& transcript() const { return transcript_; }
	core::String representation_text() const;
	core::String format_transcript() const;
	LanguageAiSnapshot snapshot() const;

private:
	OllamaConfig ollama_config_;
	core::Array<ChatMessage> transcript_;
	core::String last_user_message_;
	core::String last_assistant_message_;
	core::String status_text_ = "Language AI runtime idle.";
	bool runtime_enabled_ = true;
	bool awaiting_response_ = false;
};

METAAGENT_API LanguageAiSnapshot default_language_ai_snapshot();

} // namespace metaagent::ai
