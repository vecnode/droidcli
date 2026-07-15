#include "ffmpeg_tool.hpp"

#include "filesystem_tools.hpp"

#include <cstdlib>
#include <filesystem>

namespace droidcli::cli {
namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr const char* kFfmpegBinaryName = "ffmpeg.exe";
#else
constexpr const char* kFfmpegBinaryName = "ffmpeg";
#endif

} // namespace

FfmpegResolveResult resolve_ffmpeg()
{
	FfmpegResolveResult result;

	const WhichResult on_path = which_executable("ffmpeg");
	if (on_path.ok)
	{
		result.ok = true;
		result.resolved_path = on_path.resolved_path;
		return result;
	}

	const char* ffmpeg_root = std::getenv("DROIDCLI_FFMPEG_ROOT");
	if (ffmpeg_root != nullptr && ffmpeg_root[0] != '\0')
	{
		std::error_code error;
		const fs::path candidate = fs::path(ffmpeg_root) / "bin" / kFfmpegBinaryName;
		if (fs::exists(candidate, error) && fs::is_regular_file(candidate, error))
		{
			result.ok = true;
			result.resolved_path = fs::absolute(candidate, error).string();
			return result;
		}
	}

	result.error_message =
		"ffmpeg not found on PATH or under $DROIDCLI_FFMPEG_ROOT/bin - install it "
		"(e.g. `winget install ffmpeg` on Windows) or set DROIDCLI_FFMPEG_ROOT to a "
		"prefix with a bin/" + core::String(kFfmpegBinaryName) + " in it.";
	return result;
}

CommandRunResult run_ffmpeg(
	const core::String& args,
	const core::String& work_dir,
	const int32_t timeout_ms)
{
	const FfmpegResolveResult resolved = resolve_ffmpeg();
	if (!resolved.ok)
	{
		CommandRunResult result;
		result.error_message = resolved.error_message;
		return result;
	}

	const core::String command = "\"" + resolved.resolved_path + "\" " + args;
	// via_shell=false: ffmpeg invocations never need shell features (pipes,
	// redirects, env var expansion) and args frequently contain their own
	// nested double quotes (filter expressions like s="sin(2*PI*440)") that
	// cmd.exe's `/c` re-tokenizing pass can silently mangle - see the
	// header comment on run_command_once.
	return run_command_once(command, work_dir, timeout_ms, /*via_shell=*/false);
}

} // namespace droidcli::cli
