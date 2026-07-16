#include "clipboard.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace droidcli::cli {

#ifdef _WIN32

ClipboardWriteResult write_text_to_clipboard(const core::String& text)
{
	ClipboardWriteResult result;
	if (!OpenClipboard(nullptr))
	{
		result.error_message = "could not open the clipboard (another process may be holding it)";
		return result;
	}

	// CF_UNICODETEXT (not CF_TEXT) so non-ASCII content - emoji, non-English
	// text - survives the copy intact.
	const int wide_length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (wide_length <= 0)
	{
		CloseClipboard();
		result.error_message = "failed to convert text to UTF-16 for the clipboard";
		return result;
	}

	const HGLOBAL buffer_handle = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wide_length) * sizeof(wchar_t));
	if (buffer_handle == nullptr)
	{
		CloseClipboard();
		result.error_message = "GlobalAlloc failed";
		return result;
	}

	wchar_t* buffer = static_cast<wchar_t*>(GlobalLock(buffer_handle));
	if (buffer == nullptr)
	{
		GlobalFree(buffer_handle);
		CloseClipboard();
		result.error_message = "GlobalLock failed";
		return result;
	}
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buffer, wide_length);
	GlobalUnlock(buffer_handle);

	EmptyClipboard();
	// The clipboard owns buffer_handle after a successful SetClipboardData -
	// do not GlobalFree it ourselves.
	result.ok = SetClipboardData(CF_UNICODETEXT, buffer_handle) != nullptr;
	if (!result.ok)
	{
		GlobalFree(buffer_handle);
		result.error_message = "SetClipboardData failed";
	}
	CloseClipboard();
	return result;
}

ClipboardReadResult read_text_from_clipboard()
{
	ClipboardReadResult result;
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
	{
		result.error_message = "clipboard does not currently hold text";
		return result;
	}
	if (!OpenClipboard(nullptr))
	{
		result.error_message = "could not open the clipboard (another process may be holding it)";
		return result;
	}

	const HANDLE data_handle = GetClipboardData(CF_UNICODETEXT);
	if (data_handle == nullptr)
	{
		CloseClipboard();
		result.error_message = "GetClipboardData failed";
		return result;
	}

	const wchar_t* wide_text = static_cast<const wchar_t*>(GlobalLock(data_handle));
	if (wide_text == nullptr)
	{
		CloseClipboard();
		result.error_message = "GlobalLock failed";
		return result;
	}

	const int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, nullptr, 0, nullptr, nullptr);
	if (utf8_length > 1)
	{
		result.text.resize(static_cast<size_t>(utf8_length) - 1);
		WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, result.text.data(), utf8_length, nullptr, nullptr);
		result.ok = true;
	}
	else
	{
		result.error_message = "clipboard text was empty";
	}

	GlobalUnlock(data_handle);
	CloseClipboard();
	return result;
}

#else

ClipboardWriteResult write_text_to_clipboard(const core::String& text)
{
	(void)text;
	return ClipboardWriteResult{false, "clipboard access is not implemented on this platform"};
}

ClipboardReadResult read_text_from_clipboard()
{
	return ClipboardReadResult{false, {}, "clipboard access is not implemented on this platform"};
}

#endif

} // namespace droidcli::cli
