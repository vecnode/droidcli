#pragma once

#include "export.hpp"
#include "net/handlers.hpp"
#include "net/types.hpp"

namespace droidcli::net {

struct RouteDispatchResult {
	bool handled = false;
	HttpResponse response;
	NotifyHandleResult notify;
};

class RouteTable {
public:
	DROIDCLI_API RouteDispatchResult dispatch(
		const HttpRequest& request,
		const HandlerContext& context) const;
};

} // namespace droidcli::net
