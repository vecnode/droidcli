#pragma once

#include "droidcli_core.h"

namespace droidcli::cli {

enum class RegistryRoot {
	CurrentUser,
	LocalMachine,
};

// droidcli-infra's single registry-read primitive: opens `subkey` under
// `root`, reads a string (REG_SZ/REG_EXPAND_SZ) value - the unnamed default
// if `value_name` is empty - and closes the key. Returns an empty string on
// any failure (key/value missing, wrong type). No-op returning empty on a
// non-Windows build. command_runner's App Paths lookup, system_info's OS
// version detection, and hardware_info's CPU name detection all go through
// this instead of each independently pairing RegOpenKeyExA/RegQueryValueExA/
// RegCloseKey. app_index's own registry helpers stay local rather than being
// folded in here - they read many values from one key already held open
// across an enumeration loop, a genuinely different access pattern than
// "open once, read one value, close."
core::String read_registry_string(
	RegistryRoot root,
	const core::String& subkey,
	const core::String& value_name = {});

// Same open/read/close shape, for a REG_DWORD value. Returns false (leaving
// `out_value` untouched) on any failure.
bool read_registry_dword(
	RegistryRoot root,
	const core::String& subkey,
	const core::String& value_name,
	uint32_t& out_value);

} // namespace droidcli::cli
