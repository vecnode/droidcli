#pragma once

#include "core/types.hpp"

namespace metaagent::net {

enum class HttpMethod {
	Get,
	Post,
	Unknown,
};

enum class HttpStatus : int32_t {
	Ok = 200,
	BadRequest = 400,
	NotFound = 404,
	InternalError = 500,
};

struct HttpRequest {
	HttpMethod method = HttpMethod::Unknown;
	core::String path;
	core::String query_string;
	core::String body;
};

struct HttpResponse {
	HttpStatus status = HttpStatus::Ok;
	core::String content_type = "application/json";
	core::String body;
};

} // namespace metaagent::net
