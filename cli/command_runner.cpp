#include "command_runner.hpp"

#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace droidcli::cli {

#ifdef _WIN32

namespace {

bool looks_like_path(const core::String& value)
{
	return value.find('\\') != core::String::npos || value.find('/') != core::String::npos;
}

// Windows' "App Paths" registry mechanism - the same one Explorer/Win+R use
// to resolve a bare name like "chrome" to its actual install location even
// when the app was never added to PATH (most GUI app installers register
// here instead of touching PATH). Checked before falling back to letting
// CreateProcess do its own bare-name search. Returns an empty string if no
// match is found in either hive.
core::String resolve_app_paths_registry(const core::String& name)
{
	core::String key_name = name;
	if (key_name.size() < 4 || key_name.compare(key_name.size() - 4, 4, ".exe") != 0)
	{
		key_name += ".exe";
	}
	const core::String subkey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + key_name;

	for (const HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
	{
		HKEY key = nullptr;
		if (RegOpenKeyExA(root, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
		{
			continue;
		}

		char buffer[MAX_PATH] = {};
		DWORD buffer_size = sizeof(buffer);
		DWORD type = 0;
		const LONG query_result = RegQueryValueExA(
			key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &buffer_size);
		RegCloseKey(key);

		if (query_result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && buffer_size > 0)
		{
			return core::String(buffer);
		}
	}

	return {};
}

} // namespace

CommandRunResult run_command_once(
	const core::String& command,
	const core::String& work_dir,
	const int32_t timeout_ms,
	const bool via_shell)
{
	CommandRunResult result;
	if (command.empty())
	{
		result.error_message = "command is empty";
		return result;
	}

	SECURITY_ATTRIBUTES pipe_attributes {};
	pipe_attributes.nLength = sizeof(pipe_attributes);
	pipe_attributes.bInheritHandle = TRUE;
	pipe_attributes.lpSecurityDescriptor = nullptr;

	HANDLE stdout_read = nullptr;
	HANDLE stdout_write = nullptr;
	HANDLE stderr_read = nullptr;
	HANDLE stderr_write = nullptr;

	if (!CreatePipe(&stdout_read, &stdout_write, &pipe_attributes, 0))
	{
		result.error_message = "CreatePipe(stdout) failed";
		return result;
	}
	SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

	if (!CreatePipe(&stderr_read, &stderr_write, &pipe_attributes, 0))
	{
		CloseHandle(stdout_read);
		CloseHandle(stdout_write);
		result.error_message = "CreatePipe(stderr) failed";
		return result;
	}
	SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA startup {};
	startup.cb = sizeof(startup);
	startup.dwFlags |= STARTF_USESTDHANDLES;
	startup.hStdOutput = stdout_write;
	startup.hStdError = stderr_write;
	startup.hStdInput = nullptr;

	PROCESS_INFORMATION process {};
	// via_shell=false skips cmd.exe's own `/c` re-tokenizing pass entirely -
	// `command` is passed straight to CreateProcess, which parses it using
	// standard argv-quoting rules (the same ones ffmpeg's own argument
	// parser expects), rather than cmd.exe's separate and more fragile
	// grammar. See the header comment on run_command_once for the incident
	// this fixes.
	core::String command_line = via_shell ? ("cmd.exe /c " + command) : command;
	std::vector<char> mutable_command_line(command_line.begin(), command_line.end());
	mutable_command_line.push_back('\0');

	const char* cwd = work_dir.empty() ? nullptr : work_dir.c_str();
	const BOOL created = CreateProcessA(
		nullptr,
		mutable_command_line.data(),
		nullptr,
		nullptr,
		TRUE, // inherit handles (stdout_write/stderr_write)
		CREATE_NO_WINDOW,
		nullptr,
		cwd,
		&startup,
		&process);

	// The parent no longer needs its copies of the write ends - the child
	// (if launched) has its own inherited handles. Closing these is what lets
	// ReadFile on the read ends return 0 once the child exits.
	CloseHandle(stdout_write);
	CloseHandle(stderr_write);

	if (created == FALSE)
	{
		const DWORD last_error = GetLastError();
		CloseHandle(stdout_read);
		CloseHandle(stderr_read);
		result.error_message = "CreateProcess failed (" + std::to_string(last_error) + ")";
		return result;
	}

	result.launched = true;

	auto drain_pipe = [](HANDLE handle, core::String& out)
	{
		char buffer[4096];
		DWORD bytes_read = 0;
		while (ReadFile(handle, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0)
		{
			out.append(buffer, static_cast<size_t>(bytes_read));
		}
	};

	std::thread stdout_thread([&]() { drain_pipe(stdout_read, result.stdout_text); });
	std::thread stderr_thread([&]() { drain_pipe(stderr_read, result.stderr_text); });

	const DWORD wait_result = WaitForSingleObject(
		process.hProcess, timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : INFINITE);
	if (wait_result == WAIT_TIMEOUT)
	{
		TerminateProcess(process.hProcess, 1);
		WaitForSingleObject(process.hProcess, 2000);
		result.error_message = "command timed out after " + std::to_string(timeout_ms) + "ms";
	}

	// Reading threads unblock once the process (and its inherited pipe write
	// handles) is gone.
	stdout_thread.join();
	stderr_thread.join();

	DWORD exit_code = 0;
	GetExitCodeProcess(process.hProcess, &exit_code);
	result.exit_code = static_cast<int32_t>(exit_code);

	CloseHandle(process.hProcess);
	CloseHandle(process.hThread);
	CloseHandle(stdout_read);
	CloseHandle(stderr_read);

	return result;
}

LaunchAppResult launch_application(
	const core::String& path_or_name,
	const core::String& args,
	const core::String& work_dir)
{
	LaunchAppResult result;
	if (path_or_name.empty())
	{
		result.error_message = "path_or_name is empty";
		return result;
	}

	// A bare app name (no path separator) is checked against the App Paths
	// registry first - most installed GUI apps register there instead of
	// touching PATH, so "chrome"/"code" etc. resolve correctly even though
	// which_executable()'s PATH-only search wouldn't find them. If there's
	// no registry match, resolved_target stays as the caller's original
	// string and CreateProcess falls back to its own bare-name search
	// (calling process's directory, current directory, system directories,
	// PATH) - same as before this resolution step existed.
	core::String resolved_target = path_or_name;
	if (!looks_like_path(path_or_name))
	{
		const core::String registry_path = resolve_app_paths_registry(path_or_name);
		if (!registry_path.empty())
		{
			resolved_target = registry_path;
		}
	}

	// Quote the target in case it (or its resolved match) contains spaces;
	// args are appended as given - caller's responsibility to quote
	// individual arguments that need it.
	core::String command_line = "\"" + resolved_target + "\"";
	if (!args.empty())
	{
		command_line += " " + args;
	}
	std::vector<char> mutable_command_line(command_line.begin(), command_line.end());
	mutable_command_line.push_back('\0');

	STARTUPINFOA startup {};
	startup.cb = sizeof(startup);

	PROCESS_INFORMATION process {};
	const char* cwd = work_dir.empty() ? nullptr : work_dir.c_str();

	// No lpApplicationName: CreateProcess resolves a bare executable name
	// against the same search order a shell would (calling process's
	// directory, current directory, Windows system directories, PATH) - the
	// same intent as which_executable(), just performed by the OS itself.
	// No stdout/stderr redirection and no wait: this is a detached,
	// fire-and-forget GUI/app launch, not a captured one-shot command.
	const BOOL created = CreateProcessA(
		nullptr,
		mutable_command_line.data(),
		nullptr,
		nullptr,
		FALSE,
		0,
		nullptr,
		cwd,
		&startup,
		&process);

	if (created == FALSE)
	{
		const DWORD last_error = GetLastError();
		result.error_message = "CreateProcess failed (" + std::to_string(last_error)
			+ ") trying to launch \"" + resolved_target + "\" - not found via App Paths registry, "
			"PATH, or as a direct path. Ask the user for the exact executable name or full path "
			"rather than guessing.";
		return result;
	}

	result.launched = true;
	result.pid = static_cast<int64_t>(process.dwProcessId);
	CloseHandle(process.hProcess);
	CloseHandle(process.hThread);
	return result;
}

#else // POSIX

namespace {

// Reads whatever is currently available (non-blocking) from fd into out.
// Returns false once the peer has closed the pipe (EOF).
bool drain_available(int fd, core::String& out)
{
	char buffer[4096];
	for (;;)
	{
		const ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
		if (bytes_read > 0)
		{
			out.append(buffer, static_cast<size_t>(bytes_read));
			continue;
		}
		if (bytes_read == 0)
		{
			return false; // EOF
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			return true; // no data right now, but pipe still open
		}
		return false; // real error - treat as closed
	}
}

} // namespace

CommandRunResult run_command_once(
	const core::String& command,
	const core::String& work_dir,
	const int32_t timeout_ms,
	const bool via_shell)
{
	// via_shell has no effect here - see the header comment. Both paths
	// already go through `sh -c`, which doesn't share cmd.exe's
	// nested-quote-mangling behavior.
	(void)via_shell;

	CommandRunResult result;
	if (command.empty())
	{
		result.error_message = "command is empty";
		return result;
	}

	int stdout_pipe[2] = {-1, -1};
	int stderr_pipe[2] = {-1, -1};
	if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
	{
		result.error_message = "pipe() failed";
		return result;
	}

	const pid_t child_pid = fork();
	if (child_pid < 0)
	{
		result.error_message = "fork() failed";
		return result;
	}

	if (child_pid == 0)
	{
		// Child.
		dup2(stdout_pipe[1], STDOUT_FILENO);
		dup2(stderr_pipe[1], STDERR_FILENO);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		close(stderr_pipe[0]);
		close(stderr_pipe[1]);
		if (!work_dir.empty() && chdir(work_dir.c_str()) != 0)
		{
			_exit(127);
		}
		execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
		_exit(127);
	}

	// Parent.
	close(stdout_pipe[1]);
	close(stderr_pipe[1]);
	fcntl(stdout_pipe[0], F_SETFL, fcntl(stdout_pipe[0], F_GETFL, 0) | O_NONBLOCK);
	fcntl(stderr_pipe[0], F_SETFL, fcntl(stderr_pipe[0], F_GETFL, 0) | O_NONBLOCK);

	result.launched = true;

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 0);
	bool timed_out = false;
	int status = 0;
	bool exited = false;

	while (true)
	{
		const bool stdout_open = drain_available(stdout_pipe[0], result.stdout_text);
		const bool stderr_open = drain_available(stderr_pipe[0], result.stderr_text);

		const pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
		if (wait_result == child_pid)
		{
			exited = true;
			break;
		}

		if (!stdout_open && !stderr_open)
		{
			// Pipes closed but process not yet reaped - do a final blocking wait.
			waitpid(child_pid, &status, 0);
			exited = true;
			break;
		}

		if (timeout_ms > 0 && std::chrono::steady_clock::now() >= deadline)
		{
			timed_out = true;
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	if (timed_out)
	{
		kill(child_pid, SIGKILL);
		waitpid(child_pid, &status, 0);
		result.error_message = "command timed out after " + std::to_string(timeout_ms) + "ms";
		// Drain whatever was buffered before killing.
		drain_available(stdout_pipe[0], result.stdout_text);
		drain_available(stderr_pipe[0], result.stderr_text);
	}
	else if (exited)
	{
		if (WIFEXITED(status))
		{
			result.exit_code = WEXITSTATUS(status);
		}
		else if (WIFSIGNALED(status))
		{
			result.exit_code = 128 + WTERMSIG(status);
		}
	}

	close(stdout_pipe[0]);
	close(stderr_pipe[0]);

	return result;
}

LaunchAppResult launch_application(
	const core::String& path_or_name,
	const core::String& args,
	const core::String& work_dir)
{
	LaunchAppResult result;
	if (path_or_name.empty())
	{
		result.error_message = "path_or_name is empty";
		return result;
	}

	// Double-fork so the launched app is fully detached and re-parented to
	// init: a single fork()+exec() would leave `waitpid` below blocking
	// until the *app itself* exits (exec replaces the child's image but
	// keeps its PID), defeating fire-and-forget. The pipe hands the
	// grandchild's PID back to this process, since fork()'s return value in
	// the middle child isn't visible here.
	int pid_pipe[2] = {-1, -1};
	if (pipe(pid_pipe) != 0)
	{
		result.error_message = "pipe() failed";
		return result;
	}

	const pid_t middle_pid = fork();
	if (middle_pid < 0)
	{
		close(pid_pipe[0]);
		close(pid_pipe[1]);
		result.error_message = "fork() failed";
		return result;
	}

	if (middle_pid == 0)
	{
		// Middle child: detach into its own session, fork the real
		// grandchild, report its PID to the parent, then exit immediately -
		// orphaning the grandchild to init rather than droidcli.
		close(pid_pipe[0]);
		setsid();

		const pid_t app_pid = fork();
		if (app_pid < 0)
		{
			_exit(1);
		}
		if (app_pid == 0)
		{
			if (!work_dir.empty() && chdir(work_dir.c_str()) != 0)
			{
				_exit(127);
			}
			core::String command_line = path_or_name;
			if (!args.empty())
			{
				command_line += " " + args;
			}
			execl("/bin/sh", "sh", "-c", command_line.c_str(), static_cast<char*>(nullptr));
			_exit(127);
		}

		const int64_t pid_to_report = static_cast<int64_t>(app_pid);
		write(pid_pipe[1], &pid_to_report, sizeof(pid_to_report));
		close(pid_pipe[1]);
		_exit(0);
	}

	// Parent: reap the middle child (returns quickly, it exits right after
	// forking) and read back the actual app's PID - not a blocking wait on
	// the launched application.
	close(pid_pipe[1]);
	int64_t app_pid = 0;
	const ssize_t bytes_read = read(pid_pipe[0], &app_pid, sizeof(app_pid));
	close(pid_pipe[0]);

	int status = 0;
	waitpid(middle_pid, &status, 0);

	result.launched = bytes_read == static_cast<ssize_t>(sizeof(app_pid)) && app_pid > 0;
	result.pid = app_pid;
	if (!result.launched)
	{
		result.error_message = "failed to launch " + path_or_name;
	}
	return result;
}

#endif

} // namespace droidcli::cli
