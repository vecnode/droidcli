#pragma once

#ifndef DROIDCLI_VERSION_MAJOR
#define DROIDCLI_VERSION_MAJOR 0
#endif

#ifndef DROIDCLI_VERSION_MINOR
#define DROIDCLI_VERSION_MINOR 2
#endif

#ifndef DROIDCLI_VERSION_PATCH
#define DROIDCLI_VERSION_PATCH 0
#endif

#define DROIDCLI_VERSION_TOSTRING_IMPL(major, minor, patch) #major "." #minor "." #patch
#define DROIDCLI_VERSION_TOSTRING(major, minor, patch) DROIDCLI_VERSION_TOSTRING_IMPL(major, minor, patch)
#define DROIDCLI_VERSION_STRING \
	DROIDCLI_VERSION_TOSTRING( \
		DROIDCLI_VERSION_MAJOR, \
		DROIDCLI_VERSION_MINOR, \
		DROIDCLI_VERSION_PATCH)

namespace droidcli {

constexpr int version_major = DROIDCLI_VERSION_MAJOR;
constexpr int version_minor = DROIDCLI_VERSION_MINOR;
constexpr int version_patch = DROIDCLI_VERSION_PATCH;
constexpr const char* version_string = DROIDCLI_VERSION_STRING;

} // namespace droidcli
