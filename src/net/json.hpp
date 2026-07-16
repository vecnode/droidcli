#pragma once

#include "core/types.hpp"
#include "export.hpp"

#include <cstddef>
#include <cstdint>

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

// Parses a bare (unquoted) JSON integer field, e.g. "delay_ms":120000.
// Returns false (out_value untouched) if the field is absent or not a
// well-formed integer - callers should keep a default already assigned to
// out_value before calling, same convention as extract_json_bool_field.
DROIDCLI_API bool extract_json_int_field(
	const core::String& json,
	const core::String& field_name,
	int64_t& out_value,
	size_t search_from = 0);

} // namespace droidcli::net
