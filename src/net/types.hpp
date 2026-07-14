#pragma once

#include "core/types.hpp"

namespace droidcli::net {

enum class HttpMethod {
	Get,
	Post,
	Unknown,
};

enum class HttpStatus : int32_t {
	Ok = 200,
	BadRequest = 400,
	Unauthorized = 401,
	NotFound = 404,
	InternalError = 500,
};

struct HttpHeader {
	core::String name;
	core::String value;
};

struct HttpRequest {
	HttpMethod method = HttpMethod::Unknown;
	core::String path;
	core::String query_string;
	core::String body;
	core::Array<HttpHeader> headers;
};

struct HttpResponse {
	HttpStatus status = HttpStatus::Ok;
	core::String content_type = "application/json";
	core::String body;
};

} // namespace droidcli::net
