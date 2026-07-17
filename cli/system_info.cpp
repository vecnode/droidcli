#include "system_info.hpp"

#include "filesystem_tools.hpp"
#include "os_registry.hpp"

#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#include <limits.h>
#endif

namespace droidcli::cli {
namespace {

#ifdef _WIN32

core::String detect_architecture()
{
	SYSTEM_INFO info;
	GetNativeSystemInfo(&info);
	switch (info.wProcessorArchitecture)
	{
		case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
		case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
		case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
		default: return "unknown";
	}
}

// GetVersionEx is deprecated/lies about the version since Win8.1 unless the
// exe is manifested for the target OS, so this reads the same build number
// the registry-backed "winver" dialog shows - accurate without a manifest.
// Reads go through os_registry's shared open/read/close primitive
// (droidcli-infra), not a private RegOpenKeyExA/RegQueryValueExA pair.
core::String detect_os_version()
{
	static const core::String kSubkey = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

	uint32_t major_dword = 0;
	const core::String major_str = read_registry_dword(RegistryRoot::LocalMachine, kSubkey, "CurrentMajorVersionNumber", major_dword)
		? std::to_string(major_dword) : "0";

	uint32_t minor_dword = 0;
	const core::String minor_str = read_registry_dword(RegistryRoot::LocalMachine, kSubkey, "CurrentMinorVersionNumber", minor_dword)
		? std::to_string(minor_dword) : "0";

	core::String build_str = read_registry_string(RegistryRoot::LocalMachine, kSubkey, "CurrentBuildNumber");
	if (build_str.empty())
	{
		build_str = "0";
	}

	return major_str + "." + minor_str + "." + build_str;
}

core::String detect_hostname()
{
	char buffer[256] = {0};
	DWORD size = sizeof(buffer);
	if (GetComputerNameA(buffer, &size))
	{
		return core::String(buffer);
	}
	return "unknown";
}

core::String detect_username()
{
	char buffer[256] = {0};
	DWORD size = sizeof(buffer);
	if (GetUserNameA(buffer, &size))
	{
		return core::String(buffer);
	}
	return "unknown";
}

// Resolves any Windows Known Folder ID to a real path - not a guessed
// string concatenation (e.g. "C:\Users\" + username + "\Desktop"), which
// silently gives the wrong answer the moment a folder is OneDrive-redirected
// or the system locale renames it. Empty on failure, never a plausible-
// looking wrong path. Shared by detect_desktop_path and every other Known
// Folder lookup below - one resolution path, not five copies of the same
// PWSTR/WideCharToMultiByte dance.
core::String detect_known_folder(REFKNOWNFOLDERID folder_id)
{
	PWSTR wide_path = nullptr;
	const HRESULT result = SHGetKnownFolderPath(folder_id, 0, nullptr, &wide_path);
	if (result != S_OK || wide_path == nullptr)
	{
		if (wide_path != nullptr)
		{
			CoTaskMemFree(wide_path);
		}
		return {};
	}

	const int required = WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, nullptr, 0, nullptr, nullptr);
	core::String path;
	if (required > 1)
	{
		path.resize(static_cast<size_t>(required) - 1);
		WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, path.data(), required, nullptr, nullptr);
	}
	CoTaskMemFree(wide_path);
	return path;
}

core::String detect_desktop_path()
{
	return detect_known_folder(FOLDERID_Desktop);
}

core::String detect_home_path()
{
	return detect_known_folder(FOLDERID_Profile);
}

core::String detect_documents_path()
{
	return detect_known_folder(FOLDERID_Documents);
}

core::String detect_downloads_path()
{
	return detect_known_folder(FOLDERID_Downloads);
}

// "Where installed applications live" - the answer to a location query, not
// a substitute for the installed-apps index (a list of what's actually
// there). FOLDERID_ProgramFiles resolves to the correct Program Files for
// droidcli's own bitness (native 64-bit Program Files on a 64-bit build).
core::String detect_program_files_path()
{
	return detect_known_folder(FOLDERID_ProgramFiles);
}

#else

core::String detect_architecture()
{
	struct utsname info;
	if (uname(&info) == 0)
	{
		return core::String(info.machine);
	}
	return "unknown";
}

core::String detect_os_version()
{
	struct utsname info;
	if (uname(&info) == 0)
	{
		return core::String(info.release);
	}
	return "unknown";
}

core::String detect_hostname()
{
	char buffer[HOST_NAME_MAX + 1] = {0};
	if (gethostname(buffer, sizeof(buffer)) == 0)
	{
		return core::String(buffer);
	}
	return "unknown";
}

core::String detect_username()
{
	const char* user = std::getenv("USER");
	return user != nullptr ? core::String(user) : "unknown";
}

// Best-effort $HOME-relative guesses - POSIX has no equivalent of Windows'
// Known Folder API, and not every distro/desktop environment even has these
// folders by convention, so these are plausible guesses, not verified paths
// the way the Windows Known Folder lookups are. Empty (not a guess) if
// $HOME isn't set.
core::String detect_home_path()
{
	const char* home = std::getenv("HOME");
	return home != nullptr && home[0] != '\0' ? core::String(home) : core::String();
}

core::String detect_desktop_path()
{
	const core::String home = detect_home_path();
	return home.empty() ? core::String() : home + "/Desktop";
}

core::String detect_documents_path()
{
	const core::String home = detect_home_path();
	return home.empty() ? core::String() : home + "/Documents";
}

core::String detect_downloads_path()
{
	const core::String home = detect_home_path();
	return home.empty() ? core::String() : home + "/Downloads";
}

// No POSIX equivalent of "where installed applications live" - package
// managers scatter binaries across /usr/bin, /usr/local/bin, /opt, Flatpak/
// Snap's own trees, etc. with no single answer. Honestly empty rather than
// guessing one.
core::String detect_program_files_path()
{
	return {};
}

#endif

} // namespace

SystemInfo get_system_info()
{
	SystemInfo info;
#ifdef _WIN32
	info.os_name = "Windows";
#else
	info.os_name = "Linux";
#endif
	info.os_version = detect_os_version();
	info.architecture = detect_architecture();
	info.hostname = detect_hostname();
	info.username = detect_username();
	info.cwd = get_current_working_directory();
	info.desktop_path = detect_desktop_path();
	info.home_path = detect_home_path();
	info.documents_path = detect_documents_path();
	info.downloads_path = detect_downloads_path();
	info.program_files_path = detect_program_files_path();
	return info;
}

} // namespace droidcli::cli
