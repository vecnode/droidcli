#include "ai/ollama_client.hpp"

#include <cassert>

int main()
{
	using namespace metaagent::ai;
	using metaagent::core::Array;
	using metaagent::core::String;

	OllamaConfig config;
	config.enabled = true;
	config.base_url = "http://127.0.0.1:11434";
	config.model = "llama3.2";
	config.stream = false;
	config.temperature = 0.2f;

	assert(build_ollama_chat_url(config) == "http://127.0.0.1:11434/api/chat");

	Array<ChatMessage> messages;
	messages.push_back(ChatMessage{ChatRole::System, "You are helpful."});
	messages.push_back(ChatMessage{ChatRole::User, "Say hello."});

	const OllamaOutboundRequest request = build_ollama_chat_request(config, messages);
	assert(request.valid);
	assert(request.url == "http://127.0.0.1:11434/api/chat");
	assert(request.body.find("\"model\":\"llama3.2\"") != String::npos);
	assert(request.body.find("\"role\":\"system\"") != String::npos);
	assert(request.body.find("Say hello.") != String::npos);
	assert(request.body.find("\"stream\":false") != String::npos);
	assert(request.body.find("\"temperature\":0.2") != String::npos);

	const OllamaChatResponse ok = parse_ollama_chat_response(
		200,
		R"({"model":"llama3.2","message":{"role":"assistant","content":"Hello there."},"done":true})",
		true);
	assert(ok.http_success);
	assert(ok.done);
	assert(ok.assistant_message == "Hello there.");

	config.enabled = false;
	const OllamaOutboundRequest disabled = build_ollama_chat_request(config, messages);
	assert(!disabled.valid);

	return 0;
}
