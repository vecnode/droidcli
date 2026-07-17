#pragma once

#include "droidcli_core.h"

#include <cstdint>

namespace droidcli::cli {

// droidcli-tools (ARCHITECTURE.md's Modules diagram, Services layer) - unlike
// droidcli-infra's process/registry/window calls, these go through
// std::filesystem rather than a raw OS handle API, which is why this stays
// in Services rather than moving down to Foundations/droidcli-infra.
//
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

struct FileOpResult {
	bool ok = false;
	core::String error_message;
};

// Copies a single file from source_path to destination_path, creating
// destination parent directories if needed. Overwrites an existing
// destination file, matching write_file's existing overwrite-by-default
// convention - the gated approval step (tool_call_requires_approval,
// cli/host.cpp) is the safety net, not a silent refuse-if-exists check.
// Directories are deliberately not supported (ok:false with a clear error) -
// a narrower, safer first cut than a recursive tree copy.
FileOpResult copy_file(const core::String& source_path, const core::String& destination_path);

// Moves/renames a single file or directory from source_path to
// destination_path (std::filesystem::rename, so this also works across
// directories on the same volume). Overwrites an existing destination file,
// same convention as copy_file.
FileOpResult move_path(const core::String& source_path, const core::String& destination_path);

// Deletes a single file. Directories are deliberately not supported (ok:false
// with a clear error, no recursive delete) - the same narrower-first-cut
// reasoning as copy_file, and because an accidental recursive directory
// delete is a categorically worse mistake than an accidental file delete.
FileOpResult delete_file(const core::String& path);

// Creates a real directory (std::filesystem::create_directories, so any
// missing parent directories are created too) at path. Idempotent: ok:true
// if the directory already exists, since "create a folder" that's already
// there isn't a failure. ok:false if path already exists as a *file* -
// creating a directory over an existing file is never the right silent
// resolution. Added because "create a folder" was previously faked via
// write_file with empty content, which creates an empty *file*, not a real
// directory - a real transcript showed the user correctly notice this.
FileOpResult create_directory(const core::String& path);

} // namespace droidcli::cli
