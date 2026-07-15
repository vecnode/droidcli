#pragma once

#include "droidcli_core.h"

#include <cstdint>

namespace droidcli::cli {

// Filesystem-aware primitives for the agent tool set (POST /api/agent/turn)
// and matching /api/fs/* routes - lets the LLM know what's on disk and act
// on it directly, no external process or MCP server involved. Self-contained
// by design: droidcli executes these itself rather than delegating to
// another tool.

struct FileReadResult {
	bool ok = false;
	core::String content;
	int64_t size_bytes = 0;
	bool truncated = false;
	core::String error_message;
};

// Reads up to max_bytes of `path` as raw bytes (not required to be valid
// UTF-8/text - callers decide what to do with binary content). Caps at
// max_bytes to avoid dumping an arbitrarily large file into the LLM's
// context; truncated is set if the file was larger.
FileReadResult read_file(const core::String& path, int64_t max_bytes);

struct FileWriteResult {
	bool ok = false;
	int64_t bytes_written = 0;
	core::String error_message;
};

// Writes `content` to `path`, creating parent directories if needed.
// Overwrites by default; append_mode adds to the end of an existing file
// instead.
FileWriteResult write_file(const core::String& path, const core::String& content, bool append_mode);

struct DirEntryInfo {
	core::String name;
	bool is_dir = false;
	int64_t size_bytes = 0;
};

struct ListDirResult {
	bool ok = false;
	core::Array<DirEntryInfo> entries;
	core::String error_message;
};

// Non-recursive directory listing (one level).
ListDirResult list_dir(const core::String& path);

struct StatResult {
	bool ok = false;
	bool exists = false;
	bool is_dir = false;
	bool is_file = false;
	int64_t size_bytes = 0;
	core::String error_message;
};

// Checks existence/type/size of a path without reading its content.
StatResult stat_path(const core::String& path);

// droidcli's current working directory, as an absolute path.
core::String get_current_working_directory();

struct WhichResult {
	bool ok = false;
	core::String resolved_path;
	core::String error_message;
};

// Resolves an executable name against the PATH environment variable (and,
// on Windows, PATHEXT-style extensions), the same lookup a shell would do
// before running a bare command name.
WhichResult which_executable(const core::String& name);

} // namespace droidcli::cli
