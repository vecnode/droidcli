#include "process_manager.hpp"

#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace metaagent::app_host {

ProcessManager::~ProcessManager()
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& pair : entries_)
	{
		stop_locked(pair.second);
	}
}

core::Array<ProcessInfo> ProcessManager::snapshot()
{
	std::lock_guard<std::mutex> lock(mutex_);
	core::Array<ProcessInfo> result;
	result.reserve(entries_.size());
	for (auto& pair : entries_)
	{
		refresh_locked(pair.second);
		result.push_back(pair.second.info);
	}
	return result;
}

#ifdef _WIN32

bool ProcessManager::launch(
	const core::String& key,
	const core::String& label,
	const core::String& working_dir,
	const core::String& command,
	core::String& error_out)
{
	std::lock_guard<std::mutex> lock(mutex_);
	Entry& entry = entries_[key];
	stop_locked(entry);

	entry.info = ProcessInfo {};
	entry.info.key = key;
	entry.info.label = label;
	entry.info.command = command;
	entry.info.working_dir = working_dir;

	HANDLE job = CreateJobObjectA(nullptr, nullptr);
	if (job == nullptr)
	{
		error_out = "CreateJobObject failed";
		entry.info.status_text = error_out;
		return false;
	}

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

	// Run through cmd.exe so shell commands (make, .bat) resolve via PATH.
	// With NoDefaultCurrentDirectoryInExePath set (as on hardened systems), cmd
	// does NOT resolve bare names from the working directory — so if the command
	// starts with a file that exists in working_dir, make it explicitly relative.
	core::String effective_command = command;
	const size_t first_space = effective_command.find(' ');
	const core::String first_token =
		first_space == core::String::npos ? effective_command : effective_command.substr(0, first_space);
	if (!working_dir.empty() && first_token.find_first_of("/\\") == core::String::npos)
	{
		std::error_code ec;
		if (std::filesystem::exists(std::filesystem::path(working_dir) / first_token, ec))
		{
			effective_command = ".\\" + effective_command;
		}
	}
	core::String command_line = "cmd.exe /c " + effective_command;
	std::vector<char> mutable_command(command_line.begin(), command_line.end());
	mutable_command.push_back('\0');

	STARTUPINFOA startup {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process {};
	const char* cwd = working_dir.empty() ? nullptr : working_dir.c_str();
	const DWORD flags = CREATE_SUSPENDED | CREATE_NEW_CONSOLE;

	const BOOL created = CreateProcessA(
		nullptr, mutable_command.data(), nullptr, nullptr, FALSE, flags, nullptr, cwd, &startup, &process);
	if (created == FALSE)
	{
		const DWORD last_error = GetLastError();
		CloseHandle(job);
		error_out = "CreateProcess failed (" + std::to_string(last_error) + ")";
		entry.info.status_text = error_out;
		return false;
	}

	// Best effort: nested jobs are allowed on Windows 8+, so failure here is non-fatal.
	AssignProcessToJobObject(job, process.hProcess);
	ResumeThread(process.hThread);
	CloseHandle(process.hThread);

	entry.job_handle = job;
	entry.process_handle = process.hProcess;
	entry.info.pid = static_cast<int64_t>(process.dwProcessId);
	entry.info.running = true;
	entry.info.status_text = "running";
	error_out.clear();
	return true;
}

void ProcessManager::stop_locked(Entry& entry)
{
	if (entry.job_handle != nullptr)
	{
		TerminateJobObject(static_cast<HANDLE>(entry.job_handle), 1);
		CloseHandle(static_cast<HANDLE>(entry.job_handle));
		entry.job_handle = nullptr;
	}
	if (entry.process_handle != nullptr)
	{
		CloseHandle(static_cast<HANDLE>(entry.process_handle));
		entry.process_handle = nullptr;
	}
	if (entry.info.running)
	{
		entry.info.status_text = "stopped";
	}
	entry.info.running = false;
}

void ProcessManager::refresh_locked(Entry& entry)
{
	if (entry.process_handle == nullptr)
	{
		entry.info.running = false;
		return;
	}

	DWORD exit_code = 0;
	if (GetExitCodeProcess(static_cast<HANDLE>(entry.process_handle), &exit_code) && exit_code == STILL_ACTIVE)
	{
		entry.info.running = true;
		entry.info.status_text = "running";
		return;
	}

	entry.info.running = false;
	entry.info.status_text = "exited (" + std::to_string(exit_code) + ")";
	CloseHandle(static_cast<HANDLE>(entry.process_handle));
	entry.process_handle = nullptr;
	if (entry.job_handle != nullptr)
	{
		CloseHandle(static_cast<HANDLE>(entry.job_handle));
		entry.job_handle = nullptr;
	}
}

#else // POSIX

bool ProcessManager::launch(
	const core::String& key,
	const core::String& label,
	const core::String& working_dir,
	const core::String& command,
	core::String& error_out)
{
	std::lock_guard<std::mutex> lock(mutex_);
	Entry& entry = entries_[key];
	stop_locked(entry);

	entry.info = ProcessInfo {};
	entry.info.key = key;
	entry.info.label = label;
	entry.info.command = command;
	entry.info.working_dir = working_dir;

	const pid_t pid = fork();
	if (pid < 0)
	{
		error_out = "fork failed";
		entry.info.status_text = error_out;
		return false;
	}

	if (pid == 0)
	{
		// Child: new process group so the whole tree can be killed via killpg.
		setpgid(0, 0);
		if (!working_dir.empty())
		{
			if (chdir(working_dir.c_str()) != 0)
			{
				_exit(127);
			}
		}
		execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
		_exit(127);
	}

	setpgid(pid, pid);
	entry.pgid = pid;
	entry.info.pid = static_cast<int64_t>(pid);
	entry.info.running = true;
	entry.info.status_text = "running";
	error_out.clear();
	return true;
}

void ProcessManager::stop_locked(Entry& entry)
{
	if (entry.pgid > 0)
	{
		killpg(entry.pgid, SIGTERM);
		entry.pgid = 0;
		if (entry.info.running)
		{
			entry.info.status_text = "stopped";
		}
	}
	entry.info.running = false;
}

void ProcessManager::refresh_locked(Entry& entry)
{
	if (entry.pgid <= 0)
	{
		entry.info.running = false;
		return;
	}

	int status = 0;
	const pid_t result = waitpid(static_cast<pid_t>(entry.info.pid), &status, WNOHANG);
	if (result == 0)
	{
		entry.info.running = true;
		entry.info.status_text = "running";
		return;
	}

	entry.info.running = false;
	entry.info.status_text = "exited";
	entry.pgid = 0;
}

#endif

bool ProcessManager::stop(const core::String& key, core::String& error_out)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = entries_.find(key);
	if (it == entries_.end())
	{
		error_out = "no process for key";
		return false;
	}

#ifdef _WIN32
	const bool had_process = it->second.process_handle != nullptr;
#else
	const bool had_process = it->second.pgid > 0;
#endif
	if (!had_process)
	{
		error_out = "process not running";
		return false;
	}

	stop_locked(it->second);
	error_out.clear();
	return true;
}

} // namespace metaagent::app_host
