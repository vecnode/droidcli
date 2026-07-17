#include "classify/response_templates.hpp"

#include "net/json.hpp"

#include <string>

namespace droidcli::classify {
namespace {

core::String field(const core::String& json, const char* name)
{
	return net::extract_json_string_field(json, name);
}

bool ok_field(const core::String& json)
{
	bool value = false;
	net::extract_json_bool_field(json, "ok", value);
	return value;
}

// The path/name the caller actually asked for, before any resolution -
// used as a fallback when result_json doesn't carry a "resolved_path"
// (only present when it differs from what was asked for, per e.g.
// DroidHost::write_file's own comment).
core::String requested_path_or_name(const core::String& arguments_json)
{
	core::String value = field(arguments_json, "path_or_name");
	if (value.empty())
	{
		value = field(arguments_json, "path");
	}
	return value;
}

core::String reply_for_open_application(const core::String& arguments_json, const core::String& result_json)
{
	if (ok_field(result_json))
	{
		core::String resolved = field(result_json, "resolved_path");
		if (resolved.empty())
		{
			resolved = requested_path_or_name(arguments_json);
		}
		return "Opened " + resolved + ".";
	}
	return "Couldn't open it: " + field(result_json, "error");
}

// write_file/copy_file/move_path/delete_file/create_directory all share the
// same {"ok","error",["resolved_path"]} shape - a single template, phrased
// by verb, covers all five.
core::String reply_for_file_mutation(
	const core::String& verb_done,
	const core::String& verb_failed,
	const core::String& arguments_json,
	const core::String& result_json)
{
	if (ok_field(result_json))
	{
		core::String resolved = field(result_json, "resolved_path");
		if (resolved.empty())
		{
			resolved = requested_path_or_name(arguments_json);
		}
		return verb_done + " " + resolved + ".";
	}
	return verb_failed + ": " + field(result_json, "error");
}

core::String reply_for_write_clipboard(const core::String& result_json)
{
	if (ok_field(result_json))
	{
		return "Copied to the clipboard.";
	}
	return "Couldn't write to the clipboard: " + field(result_json, "error");
}

core::String reply_for_shell_command(const core::String& tool_name, const core::String& result_json)
{
	const bool ok = ok_field(result_json);
	int64_t exit_code = 0;
	net::extract_json_int_field(result_json, "exit_code", exit_code);
	if (ok)
	{
		return (tool_name == "run_ffmpeg" ? core::String("ffmpeg finished") : core::String("The command finished"))
			+ " successfully.";
	}
	const core::String error = field(result_json, "error");
	if (!error.empty())
	{
		return "That failed: " + error;
	}
	return "That failed (exit code " + std::to_string(exit_code) + ").";
}

} // namespace

core::String try_template_reply(
	const core::String& tool_name,
	const core::String& arguments_json,
	const core::String& result_json)
{
	if (tool_name == "open_application")
	{
		return reply_for_open_application(arguments_json, result_json);
	}
	if (tool_name == "write_file")
	{
		return reply_for_file_mutation("Wrote", "Couldn't write the file", arguments_json, result_json);
	}
	if (tool_name == "copy_file")
	{
		return reply_for_file_mutation("Copied to", "Couldn't copy the file", arguments_json, result_json);
	}
	if (tool_name == "move_path")
	{
		return reply_for_file_mutation("Moved to", "Couldn't move it", arguments_json, result_json);
	}
	if (tool_name == "delete_file")
	{
		return reply_for_file_mutation("Deleted", "Couldn't delete it", arguments_json, result_json);
	}
	if (tool_name == "create_directory")
	{
		return reply_for_file_mutation("Created", "Couldn't create the directory", arguments_json, result_json);
	}
	if (tool_name == "write_clipboard")
	{
		return reply_for_write_clipboard(result_json);
	}
	if (tool_name == "run_command" || tool_name == "run_ffmpeg")
	{
		return reply_for_shell_command(tool_name, result_json);
	}

	return {};
}

} // namespace droidcli::classify
