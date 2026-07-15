#include "window_list.hpp"

#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace droidcli::cli {

#ifdef _WIN32
namespace {

BOOL CALLBACK enum_windows_callback(HWND window, LPARAM lparam)
{
	auto* out_windows = reinterpret_cast<core::Array<OpenWindowInfo>*>(lparam);

	// Same filter Alt+Tab effectively applies: actually visible, and has a
	// title (filters out the many invisible/utility/tooltip windows every
	// running app also owns).
	if (!IsWindowVisible(window))
	{
		return TRUE;
	}
	const int title_length = GetWindowTextLengthA(window);
	if (title_length <= 0)
	{
		return TRUE;
	}

	std::vector<char> title_buffer(static_cast<size_t>(title_length) + 1, '\0');
	GetWindowTextA(window, title_buffer.data(), title_length + 1);
	const core::String title(title_buffer.data());
	if (title.empty())
	{
		return TRUE;
	}

	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	if (pid == 0)
	{
		return TRUE;
	}

	core::String process_name;
	const HANDLE process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (process_handle != nullptr)
	{
		char image_path[MAX_PATH] = {};
		DWORD path_size = sizeof(image_path);
		if (QueryFullProcessImageNameA(process_handle, 0, image_path, &path_size) && path_size > 0)
		{
			const core::String full_path(image_path);
			const size_t last_slash = full_path.find_last_of("\\/");
			process_name = last_slash == core::String::npos ? full_path : full_path.substr(last_slash + 1);
		}
		CloseHandle(process_handle);
	}

	OpenWindowInfo info;
	info.title = title;
	info.process_name = process_name;
	info.pid = static_cast<int64_t>(pid);
	out_windows->push_back(info);

	return TRUE;
}

} // namespace

core::Array<OpenWindowInfo> list_open_windows()
{
	core::Array<OpenWindowInfo> windows;
	EnumWindows(enum_windows_callback, reinterpret_cast<LPARAM>(&windows));
	return windows;
}

#else

core::Array<OpenWindowInfo> list_open_windows()
{
	return {};
}

#endif

} // namespace droidcli::cli
