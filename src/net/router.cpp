#include "net/router.hpp"

namespace metaagent::net {
namespace {

core::String normalize_path(core::String path)
{
	while (path.size() > 1 && path.back() == '/')
	{
		path.pop_back();
	}
	return path;
}

} // namespace

RouteDispatchResult RouteTable::dispatch(
	const HttpRequest& request,
	const HandlerContext& context) const
{
	const core::String path = normalize_path(request.path);

	if (request.method == HttpMethod::Get && path == "/health")
	{
		RouteDispatchResult result;
		result.handled = true;
		result.response = handle_health(context);
		return result;
	}

	if ((request.method == HttpMethod::Get || request.method == HttpMethod::Post) && path == "/echo")
	{
		RouteDispatchResult result;
		result.handled = true;
		result.response = handle_echo(request);
		return result;
	}

	if (request.method == HttpMethod::Post && path == "/notify")
	{
		RouteDispatchResult result;
		result.handled = true;
		result.notify = handle_notify(request);
		result.response = result.notify.response;
		return result;
	}

	return {};
}

} // namespace metaagent::net
