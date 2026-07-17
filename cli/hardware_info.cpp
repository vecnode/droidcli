#include "hardware_info.hpp"

#include "os_registry.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/statvfs.h>
#include <unistd.h>
#include <cstdio>
#endif

namespace droidcli::cli {
namespace {

#ifdef _WIN32

// Same registry location Task Manager/System Information read the CPU name
// from - no WMI/COM dependency needed for a value that's already sitting in
// the registry. Goes through os_registry's shared open/read/close primitive
// (droidcli-infra), not a private RegOpenKeyExA/RegQueryValueExA pair.
core::String detect_cpu_name()
{
	const core::String name = read_registry_string(
		RegistryRoot::LocalMachine,
		"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
		"ProcessorNameString");
	return name.empty() ? "unknown" : name;
}

int32_t detect_cpu_core_count()
{
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return static_cast<int32_t>(info.dwNumberOfProcessors);
}

int64_t detect_total_ram_bytes()
{
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	if (!GlobalMemoryStatusEx(&status))
	{
		return 0;
	}
	return static_cast<int64_t>(status.ullTotalPhys);
}

// EnumDisplayDevicesA (user32, already an implicit link on this target - see
// window_list.cpp's EnumWindows) rather than WMI/DXGI: no COM initialization,
// no new link dependency, and it's the same call the old Windows "Display
// adapter properties" dialog is backed by.
core::Array<GpuAdapter> detect_gpus()
{
	core::Array<GpuAdapter> gpus;
	DISPLAY_DEVICEA device;
	device.cb = sizeof(device);
	for (DWORD index = 0; EnumDisplayDevicesA(nullptr, index, &device, 0); ++index)
	{
		// DISPLAY_DEVICE_ATTACHED_TO_DESKTOP filters out disabled/phantom
		// adapters Windows still enumerates (e.g. a disconnected dock's GPU).
		if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0)
		{
			gpus.push_back(GpuAdapter{ core::String(device.DeviceString) });
		}
	}
	return gpus;
}

core::Array<DiskVolume> detect_disks()
{
	core::Array<DiskVolume> disks;
	const DWORD drive_mask = GetLogicalDrives();
	for (int letter = 0; letter < 26; ++letter)
	{
		if ((drive_mask & (1u << letter)) == 0)
		{
			continue;
		}
		core::String root = core::String(1, static_cast<char>('A' + letter)) + ":\\";
		if (GetDriveTypeA(root.c_str()) != DRIVE_FIXED)
		{
			// Skips removable/network/CD drives - "what's this machine made
			// of" means its own fixed storage, not every mapped network share.
			continue;
		}

		ULARGE_INTEGER free_bytes { };
		ULARGE_INTEGER total_bytes { };
		if (!GetDiskFreeSpaceExA(root.c_str(), &free_bytes, &total_bytes, nullptr))
		{
			continue;
		}

		DiskVolume volume;
		volume.drive = root;
		volume.total_bytes = static_cast<int64_t>(total_bytes.QuadPart);
		volume.free_bytes = static_cast<int64_t>(free_bytes.QuadPart);
		disks.push_back(volume);
	}
	return disks;
}

#else

// Best-effort POSIX equivalents - deliberately minimal, no /proc/cpuinfo
// parsing beyond the model-name line, since this is scoped to "what is this
// machine made of," not a full hwinfo/lshw replacement.
core::String detect_cpu_name()
{
	std::FILE* file = std::fopen("/proc/cpuinfo", "r");
	if (file == nullptr)
	{
		return "unknown";
	}
	char line[512];
	core::String name = "unknown";
	while (std::fgets(line, sizeof(line), file) != nullptr)
	{
		core::String text(line);
		if (text.rfind("model name", 0) == 0)
		{
			const size_t colon = text.find(':');
			if (colon != core::String::npos)
			{
				name = text.substr(colon + 2);
				if (!name.empty() && name.back() == '\n')
				{
					name.pop_back();
				}
			}
			break;
		}
	}
	std::fclose(file);
	return name;
}

int32_t detect_cpu_core_count()
{
	const long count = sysconf(_SC_NPROCESSORS_ONLN);
	return count > 0 ? static_cast<int32_t>(count) : 0;
}

int64_t detect_total_ram_bytes()
{
	const long pages = sysconf(_SC_PHYS_PAGES);
	const long page_size = sysconf(_SC_PAGE_SIZE);
	if (pages <= 0 || page_size <= 0)
	{
		return 0;
	}
	return static_cast<int64_t>(pages) * static_cast<int64_t>(page_size);
}

// GPU enumeration has no single portable POSIX API (varies by distro/driver
// stack) - deliberately left empty rather than guessing via a fragile lspci
// shell-out, which would also violate the "no shelling out for a core query"
// spirit every other detect_* function here follows.
core::Array<GpuAdapter> detect_gpus()
{
	return {};
}

core::Array<DiskVolume> detect_disks()
{
	core::Array<DiskVolume> disks;
	struct statvfs stats { };
	if (statvfs("/", &stats) == 0)
	{
		DiskVolume volume;
		volume.drive = "/";
		volume.total_bytes = static_cast<int64_t>(stats.f_blocks) * static_cast<int64_t>(stats.f_frsize);
		volume.free_bytes = static_cast<int64_t>(stats.f_bavail) * static_cast<int64_t>(stats.f_frsize);
		disks.push_back(volume);
	}
	return disks;
}

#endif

} // namespace

HardwareInfo scan_hardware_info()
{
	HardwareInfo info;
	info.cpu_name = detect_cpu_name();
	info.cpu_core_count = detect_cpu_core_count();
	info.total_ram_bytes = detect_total_ram_bytes();
	info.gpus = detect_gpus();
	info.disks = detect_disks();
	return info;
}

} // namespace droidcli::cli
