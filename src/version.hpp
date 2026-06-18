#pragma once

#ifndef METAAGENT_VERSION_MAJOR
#define METAAGENT_VERSION_MAJOR 0
#endif

#ifndef METAAGENT_VERSION_MINOR
#define METAAGENT_VERSION_MINOR 2
#endif

#ifndef METAAGENT_VERSION_PATCH
#define METAAGENT_VERSION_PATCH 0
#endif

#define METAAGENT_VERSION_TOSTRING_IMPL(major, minor, patch) #major "." #minor "." #patch
#define METAAGENT_VERSION_TOSTRING(major, minor, patch) METAAGENT_VERSION_TOSTRING_IMPL(major, minor, patch)
#define METAAGENT_VERSION_STRING \
	METAAGENT_VERSION_TOSTRING( \
		METAAGENT_VERSION_MAJOR, \
		METAAGENT_VERSION_MINOR, \
		METAAGENT_VERSION_PATCH)

namespace metaagent {

constexpr int version_major = METAAGENT_VERSION_MAJOR;
constexpr int version_minor = METAAGENT_VERSION_MINOR;
constexpr int version_patch = METAAGENT_VERSION_PATCH;
constexpr const char* version_string = METAAGENT_VERSION_STRING;

} // namespace metaagent
