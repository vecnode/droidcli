#include "windows_service.hpp"

#include <iostream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace droidcli::cli {

#if defined(_WIN32)
namespace {

// Win32's SERVICE_TABLE_ENTRY requires a raw, non-capturing function pointer
// for ServiceMain - these globals are how it reaches back into the
// caller-supplied std::functions. Set once, before StartServiceCtrlDispatcher
// is called; run_as_windows_service is a one-shot, blocking call (droidcli
// only ever calls it once per process), not meant to be reentered.
SERVICE_STATUS_HANDLE g_status_handle = nullptr;
SERVICE_STATUS g_status{};
volatile bool g_service_running = true;
std::function<void(volatile bool&)> g_run_loop;
std::function<void()> g_on_stop;

void report_status(const DWORD current_state, const DWORD wait_hint = 0)
{
	if (!g_status_handle)
	{
		return;
	}
	g_status.dwCurrentState = current_state;
	g_status.dwWaitHint = wait_hint;
	SetServiceStatus(g_status_handle, &g_status);
}

void WINAPI service_ctrl_handler(const DWORD ctrl)
{
	if (ctrl == SERVICE_CONTROL_STOP || ctrl == SERVICE_CONTROL_SHUTDOWN)
	{
		report_status(SERVICE_STOP_PENDING, 5000);
		g_service_running = false;
	}
}

void WINAPI service_main(DWORD, LPSTR*)
{
	g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
	g_status.dwWin32ExitCode = NO_ERROR;
	g_status.dwServiceSpecificExitCode = 0;
	g_status.dwCheckPoint = 0;

	g_status_handle = RegisterServiceCtrlHandlerA(kWindowsServiceName, service_ctrl_handler);
	if (!g_status_handle)
	{
		return;
	}

	report_status(SERVICE_START_PENDING, 3000);
	report_status(SERVICE_RUNNING);

	if (g_run_loop)
	{
		g_run_loop(g_service_running);
	}

	if (g_on_stop)
	{
		g_on_stop();
	}

	report_status(SERVICE_STOPPED);
}

} // namespace
#endif // _WIN32

bool run_as_windows_service(
	const std::function<void(volatile bool&)>& run_loop,
	const std::function<void()>& on_stop)
{
#if defined(_WIN32)
	g_run_loop = run_loop;
	g_on_stop = on_stop;
	g_service_running = true;

	SERVICE_TABLE_ENTRYA table[] = {
		{ const_cast<LPSTR>(kWindowsServiceName), service_main },
		{ nullptr, nullptr }
	};

	// Blocks until service_main returns (i.e. until the SCM stops us) if
	// this process was actually launched by the SCM. Returns FALSE
	// immediately - without blocking - if it wasn't (e.g. run directly from
	// a console); GetLastError() is ERROR_FAILED_SERVICE_CONTROLLER_CONNECT
	// in that case.
	return StartServiceCtrlDispatcherA(table) != 0;
#else
	(void)run_loop;
	(void)on_stop;
	return false;
#endif
}

bool install_windows_service(const core::String& command_line)
{
#if defined(_WIN32)
	const SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
	if (!scm)
	{
		std::cerr << "droidcli: OpenSCManager failed (error " << GetLastError()
			<< ") - are you running as Administrator?" << std::endl;
		return false;
	}

	const SC_HANDLE service = CreateServiceA(
		scm,
		kWindowsServiceName,
		"droidcli agent daemon",
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START,
		SERVICE_ERROR_NORMAL,
		command_line.c_str(),
		nullptr, nullptr, nullptr, nullptr, nullptr);

	const bool ok = service != nullptr;
	if (!ok)
	{
		std::cerr << "droidcli: CreateService failed (error " << GetLastError() << ")" << std::endl;
	}
	else
	{
		CloseServiceHandle(service);
	}
	CloseServiceHandle(scm);
	return ok;
#else
	(void)command_line;
	std::cerr << "droidcli: Windows Service install is only available on Windows." << std::endl;
	return false;
#endif
}

bool uninstall_windows_service()
{
#if defined(_WIN32)
	const SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (!scm)
	{
		std::cerr << "droidcli: OpenSCManager failed (error " << GetLastError()
			<< ") - are you running as Administrator?" << std::endl;
		return false;
	}
	const SC_HANDLE service = OpenServiceA(scm, kWindowsServiceName, DELETE);
	if (!service)
	{
		std::cerr << "droidcli: service \"" << kWindowsServiceName << "\" is not installed." << std::endl;
		CloseServiceHandle(scm);
		return false;
	}
	const bool ok = DeleteService(service) != 0;
	if (!ok)
	{
		std::cerr << "droidcli: DeleteService failed (error " << GetLastError() << ")" << std::endl;
	}
	CloseServiceHandle(service);
	CloseServiceHandle(scm);
	return ok;
#else
	std::cerr << "droidcli: Windows Service uninstall is only available on Windows." << std::endl;
	return false;
#endif
}

} // namespace droidcli::cli
