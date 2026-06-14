#include "metaagent.h"

#include <cassert>
#include <cstring>

int main()
{
	using namespace metaagent::net;

	HandlerContext context;
	context.session.map_name = "test_map";
	context.session.build_label = "Development";

	const HttpResponse health = handle_health(context);
	assert(health.body.find("\"status\":\"ok\"") != std::string::npos);
	assert(health.body.find("test_map") != std::string::npos);

	HttpRequest echo_request;
	echo_request.method = HttpMethod::Get;
	echo_request.path = "/echo";
	echo_request.query_string = "msg=hello";
	const HttpResponse echo = handle_echo(echo_request);
	assert(echo.body.find("hello") != std::string::npos);

	HttpRequest notify_request;
	notify_request.method = HttpMethod::Post;
	notify_request.path = "/notify";
	notify_request.body = "{\"message\":\"particle_ready\"}";
	const NotifyHandleResult notify = handle_notify(notify_request);
	assert(notify.response.body.find("true") != std::string::npos);
	assert(notify.notify_message.text == "particle_ready");

	RouteTable routes;
	HttpRequest routed_notify = notify_request;
	RouteDispatchResult dispatch = routes.dispatch(routed_notify, context);
	assert(dispatch.handled);
	assert(dispatch.notify.notify_message.text == "particle_ready");

	const metaagent::notify::NotifyParseResult parsed =
		metaagent::notify::parse_notify_body("{\"message\":\"from_core\"}");
	assert(parsed.message.text == "from_core");

	const metaagent::app::CommandResult validation = metaagent::app::validate_command(
		metaagent::app::CommandId::ToggleNetworkingRuntime,
		context.session);
	assert(validation.success);

	metaagent::ai::LanguageAiRuntime runtime;
	runtime.set_system_prompt("test");
	metaagent::ai::LanguageAiTransportCallbacks transport;
	transport.post_json = [](const std::string&, const std::string&, int32_t& status_code_out, std::string& response_body_out) {
		status_code_out = 200;
		response_body_out = R"({"message":{"role":"assistant","content":"Hi from Ollama."},"done":true})";
		return true;
	};

	HandlerContext ai_context;
	ai_context.language_ai = &runtime;
	ai_context.language_ai_transport = &transport;

	HttpRequest ai_request;
	ai_request.method = HttpMethod::Post;
	ai_request.path = "/ai/chat";
	ai_request.body = R"({"prompt":"Hello"})";
	const AiChatHandleResult ai_chat = handle_ai_chat(ai_request, ai_context);
	assert(ai_chat.completed_turn);
	assert(ai_chat.response.body.find("Hi from Ollama.") != std::string::npos);

	RouteDispatchResult ai_dispatch = routes.dispatch(ai_request, ai_context);
	assert(ai_dispatch.handled);
	assert(ai_dispatch.response.body.find("\"ok\":true") != std::string::npos);

	return 0;
}
