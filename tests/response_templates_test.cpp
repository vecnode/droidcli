#include "classify/response_templates.hpp"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::classify;
	using droidcli::core::String;

	// open_application success - prefers resolved_path over the raw request.
	{
		const String reply = try_template_reply(
			"open_application",
			"{\"path_or_name\":\"notepad\"}",
			"{\"ok\":true,\"launched\":true,\"pid\":123,\"resolved_path\":\"C:\\\\Windows\\\\notepad.exe\",\"resolution_source\":\"path_search\",\"error\":\"\"}");
		assert(reply == "Opened C:\\Windows\\notepad.exe.");
	}

	// open_application failure - falls back to the raw requested name since
	// there's no resolved_path on failure.
	{
		const String reply = try_template_reply(
			"open_application",
			"{\"path_or_name\":\"totallyfakeapp\"}",
			"{\"ok\":false,\"launched\":false,\"pid\":0,\"resolved_path\":\"\",\"resolution_source\":\"\",\"error\":\"nothing resolved\"}");
		assert(reply == "Couldn't open it: nothing resolved");
	}

	// open_application via the windows_known_location tier - prefers the
	// friendly display name over the raw exe+args path.
	{
		const String reply = try_template_reply(
			"open_application",
			"{\"path_or_name\":\"display settings\"}",
			"{\"ok\":true,\"launched\":true,\"pid\":456,\"resolved_path\":\"C:\\\\Windows\\\\System32\\\\SystemSettings.exe\",\"resolution_source\":\"windows_known_location\",\"error\":\"\",\"resolved_display_name\":\"Display Settings\"}");
		assert(reply == "Opened Display Settings.");
	}

	// write_file success.
	{
		const String reply = try_template_reply(
			"write_file",
			"{\"path\":\"C:\\\\Users\\\\x\\\\Desktop\\\\a.txt\",\"content\":\"hi\"}",
			"{\"ok\":true,\"bytes_written\":2,\"error\":\"\",\"verified_exists\":true,\"verified_size_bytes\":2}");
		assert(reply == "Wrote C:\\Users\\x\\Desktop\\a.txt.");
	}

	// write_clipboard success/failure.
	{
		const String ok_reply = try_template_reply("write_clipboard", "{\"text\":\"hi\"}", "{\"ok\":true}");
		assert(ok_reply == "Copied to the clipboard.");
		const String fail_reply = try_template_reply(
			"write_clipboard", "{\"text\":\"hi\"}", "{\"ok\":false,\"error\":\"could not open the clipboard\"}");
		assert(fail_reply == "Couldn't write to the clipboard: could not open the clipboard");
	}

	// run_command success/failure.
	{
		const String ok_reply = try_template_reply(
			"run_command", "{\"command\":\"echo hi\"}",
			"{\"ok\":true,\"launched\":true,\"exit_code\":0,\"stdout\":\"hi\",\"stderr\":\"\",\"error\":\"\"}");
		assert(ok_reply == "The command finished successfully.");
		const String fail_reply = try_template_reply(
			"run_command", "{\"command\":\"false\"}",
			"{\"ok\":false,\"launched\":true,\"exit_code\":1,\"stdout\":\"\",\"stderr\":\"\",\"error\":\"\"}");
		assert(fail_reply == "That failed (exit code 1).");
	}

	// A tool with no template - empty string forces the phrase_via_llm fallback.
	{
		const String reply = try_template_reply("list_dir", "{}", "{\"ok\":true,\"entries\":[]}");
		assert(reply.empty());
	}

	std::cout << "response_templates_test passed" << std::endl;
	return 0;
}
