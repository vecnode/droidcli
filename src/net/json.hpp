#pragma once

#include "core/types.hpp"
#include "export.hpp"

#include <cstddef>

namespace droidcli::net {

DROIDCLI_API core::String escape_json_string(const core::String& input);

DROIDCLI_API core::String json_string_field(const core::String& key, const core::String& value);

DROIDCLI_API core::String json_bool_field(const core::String& key, bool value);

DROIDCLI_API core::String extract_json_string_field(
	const core::String& json,
	const core::String& field_name,
	size_t search_from = 0);

DROIDCLI_API bool extract_json_bool_field(
	const core::String& json,
	const core::String& field_name,
	bool& out_value,
	size_t search_from = 0);

} // namespace droidcli::net
