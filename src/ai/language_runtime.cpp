#include "ai/language_runtime.hpp"

namespace droidcli::ai {
namespace {

void upsert_system_message(core::Array<ChatMessage>& transcript, const core::String& prompt)
{
	if (transcript.empty() || transcript.front().role != ChatRole::System)
	{
		transcript.insert(transcript.begin(), ChatMessage{ChatRole::System, prompt});
		return;
	}

	transcript.front().content = prompt;
}

} // namespace

void LanguageRuntime::set_ollama_config(const OpenAICompatConfig& config)
{
	ollama_config_ = config;
}

void LanguageRuntime::clear_transcript()
{
	transcript_.clear();
	last_user_message_.clear();
	last_assistant_message_.clear();
	awaiting_response_ = false;
	status_text_ = "Language AI transcript cleared.";
}

void LanguageRuntime::set_system_prompt(const core::String& prompt)
{
	if (prompt.empty())
	{
		if (!transcript_.empty() && transcript_.front().role == ChatRole::System)
		{
			transcript_.erase(transcript_.begin());
		}
		return;
	}

	upsert_system_message(transcript_, prompt);
}

bool LanguageRuntime::submit_user_message(const core::String& message)
{
	if (!runtime_enabled_)
	{
		status_text_ = "Language AI runtime is disabled.";
		return false;
	}

	if (!ollama_config_.enabled)
	{
		status_text_ = "Ollama integration is disabled.";
		return false;
	}

	if (message.empty())
	{
		status_text_ = "User message is empty.";
		return false;
	}

	if (awaiting_response_)
	{
		status_text_ = "Waiting for the previous Ollama response.";
		return false;
	}

	transcript_.push_back(ChatMessage{ChatRole::User, message});
	last_user_message_ = message;
	awaiting_response_ = true;
	status_text_ = "User message queued for Ollama.";
	return true;
}

OpenAICompatOutboundRequest LanguageRuntime::build_pending_chat_request() const
{
	if (!runtime_enabled_)
	{
		OpenAICompatOutboundRequest request;
		request.error_message = "Language AI runtime is disabled.";
		return request;
	}

	if (!awaiting_response_)
	{
		OpenAICompatOutboundRequest request;
		request.error_message = "No pending user message.";
		return request;
	}

	return build_openai_chat_request(ollama_config_, transcript_);
}

bool LanguageRuntime::apply_chat_response(const OpenAICompatChatResponse& response)
{
	if (!awaiting_response_)
	{
		status_text_ = "No pending Ollama request.";
		return false;
	}

	if (!response.transport_ok || !response.http_success)
	{
		status_text_ = response.error_message.empty()
			? "Ollama request failed."
			: response.error_message;
		return false;
	}

	if (response.assistant_message.empty())
	{
		status_text_ = response.error_message.empty()
			? "Ollama returned an empty assistant message."
			: response.error_message;
		return false;
	}

	transcript_.push_back(ChatMessage{ChatRole::Assistant, response.assistant_message});
	last_assistant_message_ = response.assistant_message;
	awaiting_response_ = false;
	status_text_ = "Ollama response applied.";
	return true;
}

bool LanguageRuntime::complete_turn(const LanguageTransportCallbacks& transport)
{
	const OpenAICompatOutboundRequest request = build_pending_chat_request();
	if (!request.valid)
	{
		status_text_ = request.error_message;
		return false;
	}

	if (!transport.post_json)
	{
		status_text_ = "Language AI transport is not bound.";
		return false;
	}

	int32_t status_code = 0;
	core::String response_body;
	const bool transport_ok = transport.post_json(request.url, request.body, {}, status_code, response_body);
	return apply_chat_response(parse_openai_chat_response(status_code, response_body, transport_ok));
}

core::String LanguageRuntime::representation_text() const
{
	if (!last_assistant_message_.empty())
	{
		return last_assistant_message_;
	}

	if (!last_user_message_.empty())
	{
		return last_user_message_;
	}

	return status_text_;
}

core::String LanguageRuntime::format_transcript() const
{
	core::String formatted;
	for (size_t index = 0; index < transcript_.size(); ++index)
	{
		if (index > 0)
		{
			formatted += "\n";
		}

		const ChatMessage& message = transcript_[index];
		formatted += chat_role_to_string(message.role);
		formatted += ": ";
		formatted += message.content;
	}

	return formatted;
}

LanguageSnapshot LanguageRuntime::snapshot() const
{
	LanguageSnapshot snapshot;
	snapshot.runtime_enabled = runtime_enabled_;
	snapshot.connected = ollama_config_.enabled && !ollama_config_.base_url.empty();
	snapshot.awaiting_response = awaiting_response_;
	snapshot.model = ollama_config_.model;
	snapshot.status_text = status_text_;
	snapshot.last_user_message = last_user_message_;
	snapshot.last_assistant_message = last_assistant_message_;
	snapshot.representation_text = representation_text();
	snapshot.transcript_length = static_cast<int32_t>(transcript_.size());
	return snapshot;
}

LanguageSnapshot default_language_ai_snapshot()
{
	LanguageSnapshot snapshot;
	snapshot.status_text = "Language AI runtime not initialized.";
	return snapshot;
}

} // namespace droidcli::ai
