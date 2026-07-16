#pragma once

#include "droidcli_core.h"

namespace droidcli::cli {

// OS clipboard access for the agent tool set (POST /api/agent/turn) and the
// TUI's own transcript-copy keybinding ('y') - one implementation, not two.
// Windows-only for now, matching this codebase's existing Windows-first
// precedent (command_runner.cpp, process_manager.cpp); a POSIX
// implementation (X11/Wayland clipboard, or shelling out to pbcopy on
// macOS) is a reasonable future addition but not implemented here.

struct ClipboardReadResult {
	bool ok = false;
	core::String text;
	core::String error_message;
};

// Reads the current OS clipboard as UTF-8 text. ok:false (with a clear
// error, not a crash) if the clipboard is empty, holds non-text content
// (an image, a file selection), or clipboard access fails outright.
ClipboardReadResult read_text_from_clipboard();

struct ClipboardWriteResult {
	bool ok = false;
	core::String error_message;
};

// Writes `text` (UTF-8) to the OS clipboard, replacing its current content.
ClipboardWriteResult write_text_to_clipboard(const core::String& text);

} // namespace droidcli::cli
