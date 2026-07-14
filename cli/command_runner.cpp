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

CommandRunResult run_command_once(
	const core::String& command,
	const core::String& work_dir,
	const int32_t timeout_ms)
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
	core::String command_line = "cmd.exe /c " + command;
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
	const int32_t timeout_ms)
{
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

#endif

} // namespace droidcli::cli
