#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace metaagent::net {

METAAGENT_API core::String escape_json_string(const core::String& input);

METAAGENT_API core::String json_string_field(const core::String& key, const core::String& value);

METAAGENT_API core::String json_bool_field(const core::String& key, bool value);

} // namespace metaagent::net
