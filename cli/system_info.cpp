#include "system_info.hpp"

#include "filesystem_tools.hpp"

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
core::String detect_os_version()
{
	HKEY key;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) != ERROR_SUCCESS)
	{
		return "unknown";
	}

	core::String major_str = "0";
	core::String minor_str = "0";
	core::String build_str = "0";

	DWORD major_dword = 0;
	DWORD dword_size = sizeof(DWORD);
	if (RegQueryValueExA(key, "CurrentMajorVersionNumber", nullptr, nullptr, reinterpret_cast<LPBYTE>(&major_dword), &dword_size) == ERROR_SUCCESS)
	{
		major_str = std::to_string(major_dword);
	}

	DWORD minor_dword = 0;
	dword_size = sizeof(DWORD);
	if (RegQueryValueExA(key, "CurrentMinorVersionNumber", nullptr, nullptr, reinterpret_cast<LPBYTE>(&minor_dword), &dword_size) == ERROR_SUCCESS)
	{
		minor_str = std::to_string(minor_dword);
	}

	char build[32] = {0};
	DWORD build_size = sizeof(build);
	if (RegQueryValueExA(key, "CurrentBuildNumber", nullptr, nullptr, reinterpret_cast<LPBYTE>(build), &build_size) == ERROR_SUCCESS)
	{
		build_str = build;
	}

	RegCloseKey(key);
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

// The real Desktop folder via the Windows Known Folder API - not a guessed
// "C:\Users\" + username + "\Desktop" concatenation, which silently gives
// the wrong answer the moment the Desktop is OneDrive-redirected or the
// system locale renames the folder. Empty on failure, never a plausible-
// looking wrong path.
core::String detect_desktop_path()
{
	PWSTR wide_path = nullptr;
	const HRESULT result = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &wide_path);
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

// Best-effort $HOME/Desktop - POSIX has no equivalent of Windows' Known
// Folder API, and not every distro/desktop environment even has a Desktop
// folder by convention, so this is a plausible guess, not a verified path
// the way detect_desktop_path() is on Windows. Empty (not a guess) if
// $HOME isn't set.
core::String detect_desktop_path()
{
	const char* home = std::getenv("HOME");
	if (home == nullptr || home[0] == '\0')
	{
		return {};
	}
	return core::String(home) + "/Desktop";
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
	return info;
}

} // namespace droidcli::cli
