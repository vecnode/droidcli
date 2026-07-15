#include "app_index.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <filesystem>
#endif

namespace droidcli::cli {

#ifdef _WIN32
namespace {

namespace fs = std::filesystem;

core::String read_registry_string(HKEY key, const char* value_name)
{
	DWORD type = 0;
	DWORD size = 0;
	if (RegQueryValueExA(key, value_name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0)
	{
		return {};
	}
	if (type != REG_SZ && type != REG_EXPAND_SZ)
	{
		return {};
	}

	std::vector<char> buffer(static_cast<size_t>(size) + 1, '\0');
	if (RegQueryValueExA(key, value_name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS)
	{
		return {};
	}
	return core::String(buffer.data());
}

bool read_registry_dword(HKEY key, const char* value_name, DWORD& out_value)
{
	DWORD type = 0;
	DWORD size = sizeof(DWORD);
	if (RegQueryValueExA(key, value_name, nullptr, &type, reinterpret_cast<LPBYTE>(&out_value), &size) != ERROR_SUCCESS)
	{
		return false;
	}
	return type == REG_DWORD;
}

// "C:\Path\App.exe,0" -> "C:\Path\App.exe". Only strips a trailing comma
// suffix if what follows it is a plain (optionally negative) integer, since
// some installers legitimately put a comma inside the path itself.
core::String strip_icon_index(const core::String& display_icon)
{
	const size_t comma = display_icon.rfind(',');
	if (comma == core::String::npos)
	{
		return display_icon;
	}

	bool looks_like_index = comma + 1 < display_icon.size();
	for (size_t i = comma + 1; i < display_icon.size() && looks_like_index; ++i)
	{
		if (std::isdigit(static_cast<unsigned char>(display_icon[i])) == 0 && display_icon[i] != '-')
		{
			looks_like_index = false;
		}
	}
	return looks_like_index ? display_icon.substr(0, comma) : display_icon;
}

core::String to_lower(const core::String& value)
{
	core::String result = value;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

// Some installers put a non-executable path in DisplayIcon (a bare .ico
// file, an Uninstall.exe, etc.) - checking existence alone isn't enough to
// call it a usable launch target.
bool is_usable_exe_path(const fs::path& path)
{
	std::error_code error;
	return to_lower(path.extension().string()) == ".exe"
		&& fs::exists(path, error) && !fs::is_directory(path, error);
}

// Shallow (depth-limited) search of `directory` for a .exe whose stem
// resembles `display_name`, falling back to the first .exe found. Used when
// DisplayIcon isn't a usable direct path - InstallLocation points at a
// folder, not a specific executable, so this is a best-effort guess.
core::String find_exe_in_directory(const fs::path& directory, const core::String& display_name)
{
	if (directory.empty())
	{
		return {};
	}
	std::error_code error;
	if (!fs::exists(directory, error) || !fs::is_directory(directory, error))
	{
		return {};
	}

	const core::String lower_name = to_lower(display_name);
	core::String fallback;
	constexpr int kMaxDepth = 2;

	fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, error);
	const fs::recursive_directory_iterator end;
	for (; it != end && !error; it.increment(error))
	{
		if (it.depth() > kMaxDepth)
		{
			it.disable_recursion_pending();
			continue;
		}
		if (it->path().extension() != ".exe")
		{
			continue;
		}

		const core::String stem = to_lower(it->path().stem().string());
		if (!lower_name.empty()
			&& (lower_name.find(stem) != core::String::npos || stem.find(lower_name) != core::String::npos))
		{
			return it->path().string();
		}
		if (fallback.empty())
		{
			fallback = it->path().string();
		}
	}

	return fallback;
}

void scan_uninstall_hive(HKEY root, const char* subkey_path, core::Array<InstalledApp>& out_apps)
{
	HKEY uninstall_key = nullptr;
	if (RegOpenKeyExA(root, subkey_path, 0, KEY_READ, &uninstall_key) != ERROR_SUCCESS)
	{
		return;
	}

	for (DWORD index = 0;; ++index)
	{
		char subkey_name[256] = {};
		DWORD subkey_name_size = sizeof(subkey_name);
		const LONG enum_result = RegEnumKeyExA(
			uninstall_key, index, subkey_name, &subkey_name_size, nullptr, nullptr, nullptr, nullptr);
		if (enum_result == ERROR_NO_MORE_ITEMS)
		{
			break;
		}
		if (enum_result != ERROR_SUCCESS)
		{
			continue;
		}

		HKEY entry_key = nullptr;
		if (RegOpenKeyExA(uninstall_key, subkey_name, 0, KEY_READ, &entry_key) != ERROR_SUCCESS)
		{
			continue;
		}

		DWORD system_component = 0;
		read_registry_dword(entry_key, "SystemComponent", system_component);

		const core::String display_name = read_registry_string(entry_key, "DisplayName");
		if (!display_name.empty() && system_component == 0)
		{
			core::String path = strip_icon_index(read_registry_string(entry_key, "DisplayIcon"));
			if (path.empty() || !is_usable_exe_path(fs::path(path)))
			{
				const core::String install_location = read_registry_string(entry_key, "InstallLocation");
				path = find_exe_in_directory(fs::path(install_location), display_name);
			}

			if (!path.empty())
			{
				out_apps.push_back(InstalledApp{display_name, path});
			}
		}

		RegCloseKey(entry_key);
	}

	RegCloseKey(uninstall_key);
}

// Windows accessories/system tools (Notepad, Calculator, Paint, ...) are not
// installed applications in the Add/Remove Programs sense - they ship with
// the OS and never register an Uninstall entry, so scan_uninstall_hive()
// above can never find them. They resolve fine via bare PATH lookup
// (System32 is always on PATH) once launch_application() tries them, but
// find_application()/find_installed_app_match() only searches this index -
// without an entry here, a query like "notepad" or "calculator" comes back
// with zero matches even though opening it would have worked. Listing them
// explicitly closes that gap and lets find_application answer confidently
// for the apps a user is most likely to ask for by a common name.
void add_builtin_accessories(core::Array<InstalledApp>& out_apps)
{
	static const struct { const char* name; const char* exe; } kBuiltins[] = {
		{"Notepad", "notepad.exe"},
		{"Calculator", "calc.exe"},
		{"Paint", "mspaint.exe"},
		{"WordPad", "write.exe"},
		{"Command Prompt", "cmd.exe"},
		{"PowerShell", "powershell.exe"},
		{"File Explorer", "explorer.exe"},
		{"Task Manager", "taskmgr.exe"},
		{"Control Panel", "control.exe"},
		{"Snipping Tool", "snippingtool.exe"},
		{"Magnifier", "magnify.exe"},
		{"Registry Editor", "regedit.exe"},
		{"Character Map", "charmap.exe"},
		{"Remote Desktop Connection", "mstsc.exe"},
		{"Disk Cleanup", "cleanmgr.exe"},
	};
	for (const auto& builtin : kBuiltins)
	{
		out_apps.push_back(InstalledApp{builtin.name, builtin.exe});
	}
}

} // namespace

core::Array<InstalledApp> scan_installed_applications()
{
	core::Array<InstalledApp> apps;
	// Native view (64-bit apps on 64-bit Windows) and the explicit
	// WOW6432Node path (32-bit apps) - using the literal WOW6432Node path
	// rather than a KEY_WOW64_32KEY flag so this works the same whether
	// droidcli itself is built 32- or 64-bit.
	scan_uninstall_hive(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", apps);
	scan_uninstall_hive(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", apps);
	scan_uninstall_hive(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", apps);
	add_builtin_accessories(apps);
	return apps;
}

#else

core::Array<InstalledApp> scan_installed_applications()
{
	return {};
}

#endif

} // namespace droidcli::cli
