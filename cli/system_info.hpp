#pragma once

#include "droidcli_core.h"

namespace droidcli::cli {

// Describes the machine droidcli is actually running on right now - gathered
// once at startup (DroidHost::initialize()) via real OS queries, not
// hardcoded or assumed. Distinct from RuntimeSession (droidcli's own
// map_name/build_label/feature-flag state): SystemInfo is about the host
// environment droidcli lives in, not droidcli itself.
struct SystemInfo {
	core::String os_name;        // "Windows" or "Linux"
	core::String os_version;     // e.g. "10.0.26200" (Windows) or uname release (Linux)
	core::String architecture;   // e.g. "x64", "x86", "arm64"
	core::String hostname;
	core::String username;
	core::String cwd;            // same as get_current_working_directory()
};

// Queries the OS for name/version/architecture/hostname/username/cwd. Cheap
// enough to call once at startup and cache - not meant to be polled.
SystemInfo get_system_info();

} // namespace droidcli::cli
