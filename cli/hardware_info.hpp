#pragma once

#include "droidcli_core.h"

namespace droidcli::cli {

// Part of droidcli-infra (ARCHITECTURE.md's Modules diagram) - queries the
// OS directly (registry, display/disk enumeration), not droidcli-tools.
//
// One GPU/display adapter's name, as reported by the OS.
struct GpuAdapter {
	core::String name;
};

// One local fixed disk volume's capacity, in bytes.
struct DiskVolume {
	core::String drive;       // e.g. "C:\\" (Windows) or a mount point (POSIX)
	int64_t total_bytes = 0;
	int64_t free_bytes = 0;
};

// Read-only local hardware inventory - CPU/GPU/RAM/disk, gathered once at
// startup the same way SystemInfo is (see system_info.hpp), not polled. This
// is deliberately NOT the ZeroClaw zeroclaw-hardware/aardvark-sys/robot-kit
// scope (GPIO/I2C/SPI/USB device *control*) - see the "droidcli-hardware"
// row and "Hardware awareness" section in ARCHITECTURE.md for why that
// distinction matters: this answers "what is this machine made of," it does
// not add any capability to drive a peripheral.
struct HardwareInfo {
	core::String cpu_name;
	int32_t cpu_core_count = 0;
	int64_t total_ram_bytes = 0;
	core::Array<GpuAdapter> gpus;
	core::Array<DiskVolume> disks;
};

// Queries the OS for CPU name/core count, total RAM, GPU adapter name(s),
// and per-volume disk capacity. Only called when hardware scanning has been
// opted into (see HostConfig::enable_hardware_scan, cli/host.hpp) - this
// function itself has no consent-gate logic, it's a pure OS query, same
// division of concern as get_system_info().
HardwareInfo scan_hardware_info();

} // namespace droidcli::cli
