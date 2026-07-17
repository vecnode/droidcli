#include "windows_locations.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>
#include <filesystem>
#include <vector>
#else
#include <filesystem>
#endif

namespace droidcli::cli {
namespace {

struct HardcodedWindowsLocationEntry
{
	const char* alias;
	const char* display_name;
	const char* path_or_name;
	const char* args;
};

// The two categories with no path to genuine OS enumeration - see
// scan_windows_locations's header comment in windows_locations.hpp for why
// each group stays hardcoded. Everything else (known folders, Admin Tools
// shortcuts) is discovered fresh from the OS below.
static const HardcodedWindowsLocationEntry kHardcodedWindowsLocationExceptions[] = {
	// ms-settings: deep links - Microsoft exposes no API listing valid
	// sub-pages and their names, so these can never be discovered, only
	// hand-typed from observed use.
	{"sound settings", "Sound Settings", "explorer.exe", "ms-settings:sound"},
	{"display settings", "Display Settings", "explorer.exe", "ms-settings:display"},
	{"network settings", "Network Settings", "explorer.exe", "ms-settings:network"},
	{"bluetooth settings", "Bluetooth Settings", "explorer.exe", "ms-settings:bluetooth"},
	{"windows update", "Windows Update", "explorer.exe", "ms-settings:windowsupdate"},
	{"windows settings", "Windows Settings", "explorer.exe", "ms-settings:"},
	{"storage settings", "Storage Settings", "explorer.exe", "ms-settings:storagesense"},
	{"about this pc", "About", "explorer.exe", "ms-settings:about"},
	{"apps and features", "Apps & Features", "explorer.exe", "ms-settings:appsfeatures"},
	{"windows security", "Windows Security", "explorer.exe", "windowsdefender:"},
	{"windows defender", "Windows Security", "explorer.exe", "windowsdefender:"},
	{"printers and scanners", "Printers & Scanners", "explorer.exe", "ms-settings:printers"},
	// Deferred Control Panel applets - genuinely enumerable via Shell
	// PIDL/IEnumIDList over the "All Control Panel Items" virtual folder,
	// not implemented yet (meaningfully fiddlier COM code, see
	// windows_locations.hpp) - a candidate for a focused follow-up.
	{"control panel", "Control Panel", "control.exe", ""},
	{"system properties", "System Properties", "control.exe", "system"},
	{"programs and features", "Programs and Features", "control.exe", "appwiz.cpl"},
	{"network connections", "Network Connections", "control.exe", "ncpa.cpl"},
	{"power options", "Power Options", "control.exe", "powercfg.cpl"},
	{"date and time", "Date and Time", "control.exe", "timedate.cpl"},
	// Stable admin one-liners - simple, version-independent direct
	// invocations not confidently guaranteed to appear as standalone Admin
	// Tools shortcuts on every Windows install (Task Manager in particular
	// typically isn't a Start Menu shortcut at all).
	{"task manager", "Task Manager", "taskmgr.exe", ""},
	{"process manager", "Task Manager", "taskmgr.exe", ""},
	{"memory usage", "Task Manager", "taskmgr.exe", ""},
	{"memory panel", "Task Manager", "taskmgr.exe", ""},
	{"cpu usage", "Task Manager", "taskmgr.exe", ""},
	{"running processes", "Task Manager", "taskmgr.exe", ""},
	{"device manager", "Device Manager", "mmc.exe", "devmgmt.msc"},
	{"disk management", "Disk Management", "mmc.exe", "diskmgmt.msc"},
	{"disk partition", "Disk Management", "mmc.exe", "diskmgmt.msc"},
	{"partition manager", "Disk Management", "mmc.exe", "diskmgmt.msc"},
	{"manage disks", "Disk Management", "mmc.exe", "diskmgmt.msc"},
};

#ifdef _WIN32

core::String wide_to_narrow(LPCWSTR wide)
{
	if (wide == nullptr)
	{
		return {};
	}
	const int required = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
	if (required <= 0)
	{
		return {};
	}
	std::vector<char> buffer(static_cast<size_t>(required));
	WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer.data(), required, nullptr, nullptr);
	return core::String(buffer.data());
}

std::vector<wchar_t> narrow_to_wide(const core::String& narrow)
{
	const int required = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, nullptr, 0);
	if (required <= 0)
	{
		return {};
	}
	std::vector<wchar_t> buffer(static_cast<size_t>(required));
	MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, buffer.data(), required);
	return buffer;
}

// COINIT_APARTMENTTHREADED is the conventional model for Shell interfaces
// (IKnownFolderManager, IShellLink). RPC_E_CHANGED_MODE means this thread
// already has COM initialized under a different model - proceed anyway
// (the existing initialization still works for these interfaces), just
// don't pair it with a CoUninitialize we didn't earn.
bool com_scope_begin(bool& should_uninitialize)
{
	const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	should_uninitialize = SUCCEEDED(result) && result != S_FALSE;
	return SUCCEEDED(result) || result == S_FALSE || result == RPC_E_CHANGED_MODE;
}

// Real known folders (Desktop, Documents, Downloads, Recycle Bin, This PC,
// ...) via IKnownFolderManager, filtered to KF_CATEGORY_COMMON/PERUSER (the
// visible, user-facing folders) rather than the ~100+ internal
// (KF_CATEGORY_VIRTUAL/FIXED) folders Windows also registers (profile-cache
// directories, per-app data folders, etc.) that would flood the alias
// table with names no user would ever ask droidcli to open.
void scan_known_folders(core::Array<WindowsLocationEntry>& out)
{
	bool should_uninitialize = false;
	if (!com_scope_begin(should_uninitialize))
	{
		return;
	}

	// IKnownFolderManager::GetFolderIds() enumerates ~200 folders total, and
	// KF_CATEGORY (COMMON/PERUSER/VIRTUAL/FIXED) does not cleanly separate
	// "a human would ask droidcli to open this by name" from internal
	// per-user redirect paths - live testing against a real machine found
	// category filtering both too loose (COMMON/PERUSER already includes
	// obscure cache/history folders) and too strict (it wrongly excluded
	// Recycle Bin/This PC/Network, which are KF_CATEGORY_VIRTUAL - the exact
	// incident that originally motivated hardcoding these three). Querying
	// a fixed, small allowlist of KNOWNFOLDERIDs directly sidesteps the
	// category problem entirely: which folders are worth exposing is still
	// a curated list (Microsoft-defined GUIDs, not app-specific magic
	// strings), but each one's *current* real display name and shell path
	// still comes from asking Windows, not from a hand-typed string - if
	// Microsoft ever renames or relocalizes one, droidcli picks that up
	// automatically instead of drifting stale like the old hardcoded table.
	static const KNOWNFOLDERID* const kKnownFolderAllowlist[] = {
		&FOLDERID_Desktop, &FOLDERID_Documents, &FOLDERID_Downloads,
		&FOLDERID_Music, &FOLDERID_Pictures, &FOLDERID_Videos,
		&FOLDERID_RecycleBinFolder, &FOLDERID_ComputerFolder, &FOLDERID_NetworkFolder,
		&FOLDERID_ControlPanelFolder, &FOLDERID_Favorites, &FOLDERID_Fonts, &FOLDERID_Public,
	};

	IKnownFolderManager* manager = nullptr;
	if (SUCCEEDED(CoCreateInstance(
		CLSID_KnownFolderManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager))) && manager != nullptr)
	{
		for (const KNOWNFOLDERID* const folder_id : kKnownFolderAllowlist)
		{
			IKnownFolder* folder = nullptr;
			if (FAILED(manager->GetFolder(*folder_id, &folder)) || folder == nullptr)
			{
				continue;
			}

			KNOWNFOLDER_DEFINITION definition = {};
			if (SUCCEEDED(folder->GetFolderDefinition(&definition)))
			{
				if (definition.pszName != nullptr)
				{
					// pszLocalizedName is often a resource reference
					// ("@shell32.dll,-21798"), not display text on its
					// own - resolve it via SHLoadIndirectString, falling
					// back to the raw canonical name if that fails.
					core::String display_name;
					if (definition.pszLocalizedName != nullptr && definition.pszLocalizedName[0] == L'@')
					{
						WCHAR resolved[512] = {};
						if (SUCCEEDED(SHLoadIndirectString(definition.pszLocalizedName, resolved, 512, nullptr)))
						{
							display_name = wide_to_narrow(resolved);
						}
					}
					else if (definition.pszLocalizedName != nullptr)
					{
						display_name = wide_to_narrow(definition.pszLocalizedName);
					}
					if (display_name.empty())
					{
						display_name = wide_to_narrow(definition.pszName);
					}

					if (!display_name.empty())
					{
						WindowsLocationEntry entry;
						entry.display_name = display_name;
						entry.alias = display_name;
						entry.path_or_name = "explorer.exe";
						entry.args = "shell:" + wide_to_narrow(definition.pszName);
						out.push_back(entry);
					}
				}
				FreeKnownFolderDefinitionFields(&definition);
			}
			folder->Release();
		}
		manager->Release();
	}

	if (should_uninitialize)
	{
		CoUninitialize();
	}
}

// Real Administrative Tools (Event Viewer, Performance Monitor, Services,
// ...) - whatever .lnk shortcuts actually exist in Windows' own
// Administrative Tools Start Menu folder on this machine, resolved via
// IShellLink and verified to actually exist before being accepted (the same
// "never propose an unverified target" rule the rest of the Windows
// execution ruleset already follows).
void scan_admin_tools(core::Array<WindowsLocationEntry>& out)
{
	PWSTR admin_tools_path = nullptr;
	const HRESULT path_result = SHGetKnownFolderPath(FOLDERID_CommonAdminTools, 0, nullptr, &admin_tools_path);
	if (FAILED(path_result) || admin_tools_path == nullptr)
	{
		if (admin_tools_path != nullptr)
		{
			CoTaskMemFree(admin_tools_path);
		}
		return;
	}
	const core::String directory = wide_to_narrow(admin_tools_path);
	CoTaskMemFree(admin_tools_path);

	std::error_code exists_error;
	if (directory.empty() || !std::filesystem::exists(directory, exists_error))
	{
		return;
	}

	bool should_uninitialize = false;
	if (!com_scope_begin(should_uninitialize))
	{
		return;
	}

	std::error_code iterate_error;
	for (const auto& dir_entry : std::filesystem::directory_iterator(directory, iterate_error))
	{
		if (iterate_error)
		{
			break;
		}
		if (dir_entry.path().extension() != ".lnk")
		{
			continue;
		}

		IShellLinkA* shell_link = nullptr;
		if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
			IID_IShellLinkA, reinterpret_cast<void**>(&shell_link))) || shell_link == nullptr)
		{
			continue;
		}

		IPersistFile* persist_file = nullptr;
		if (SUCCEEDED(shell_link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist_file)))
			&& persist_file != nullptr)
		{
			const std::vector<wchar_t> wide_path = narrow_to_wide(dir_entry.path().string());
			if (!wide_path.empty() && SUCCEEDED(persist_file->Load(wide_path.data(), STGM_READ)))
			{
				char target_buffer[MAX_PATH] = {};
				WIN32_FIND_DATAA find_data = {};
				if (SUCCEEDED(shell_link->GetPath(target_buffer, MAX_PATH, &find_data, 0)) && target_buffer[0] != '\0')
				{
					std::error_code target_exists_error;
					if (std::filesystem::exists(target_buffer, target_exists_error))
					{
						char args_buffer[1024] = {};
						shell_link->GetArguments(args_buffer, sizeof(args_buffer) - 1);

						WindowsLocationEntry entry;
						entry.display_name = dir_entry.path().stem().string();
						entry.alias = entry.display_name;
						entry.path_or_name = target_buffer;
						entry.args = args_buffer;
						out.push_back(entry);
					}
				}
			}
			persist_file->Release();
		}
		shell_link->Release();
	}

	if (should_uninitialize)
	{
		CoUninitialize();
	}
}

#else // non-Windows: no known folders / Admin Tools concept - always empty.

void scan_known_folders(core::Array<WindowsLocationEntry>&) {}
void scan_admin_tools(core::Array<WindowsLocationEntry>&) {}

#endif

} // namespace

core::Array<WindowsLocationEntry> scan_windows_locations()
{
	core::Array<WindowsLocationEntry> result;
	for (const HardcodedWindowsLocationEntry& hardcoded : kHardcodedWindowsLocationExceptions)
	{
		result.push_back(WindowsLocationEntry{
			hardcoded.alias, hardcoded.display_name, hardcoded.path_or_name, hardcoded.args});
	}
	scan_known_folders(result);
	scan_admin_tools(result);
	return result;
}

} // namespace droidcli::cli
