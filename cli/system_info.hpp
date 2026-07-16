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
	// The current user's actual Desktop folder - resolved via the Windows
	// Known Folder API (FOLDERID_Desktop), not guessed by string-concatenating
	// "C:\Users\" + username + "\Desktop", which breaks silently for a
	// OneDrive-redirected or localized Desktop folder. Empty if it couldn't
	// be resolved (e.g. the API call failed, or on a POSIX build without a
	// $HOME to fall back to) - a caller should treat that as "unknown", not
	// assume the naive path is correct.
	core::String desktop_path;
	// Same Known Folder resolution as desktop_path, same "empty means
	// unresolved, not a guess" contract - the user's home/profile folder
	// (FOLDERID_Profile), Documents (FOLDERID_Documents), Downloads
	// (FOLDERID_Downloads), and where installed applications actually live
	// (FOLDERID_ProgramFiles) - the answer to "where are the apps," distinct
	// from the installed-apps index (a list of what's there, not a path).
	core::String home_path;
	core::String documents_path;
	core::String downloads_path;
	core::String program_files_path;
};

// Queries the OS for name/version/architecture/hostname/username/cwd. Cheap
// enough to call once at startup and cache - not meant to be polled.
SystemInfo get_system_info();

} // namespace droidcli::cli
