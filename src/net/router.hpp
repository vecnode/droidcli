#pragma once

#include "export.hpp"
#include "net/handlers.hpp"
#include "net/types.hpp"

namespace metaagent::net {

struct RouteDispatchResult {
	bool handled = false;
	HttpResponse response;
	NotifyHandleResult notify;
};

class RouteTable {
public:
	METAAGENT_API RouteDispatchResult dispatch(
		const HttpRequest& request,
		const HandlerContext& context) const;
};

} // namespace metaagent::net
