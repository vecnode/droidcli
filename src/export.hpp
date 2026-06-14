#pragma once

#if defined(_WIN32) && defined(METAAGENT_BUILDING_SHARED)
#if defined(METAAGENT_BUILDING)
#define METAAGENT_API __declspec(dllexport)
#else
#define METAAGENT_API __declspec(dllimport)
#endif
#else
#define METAAGENT_API
#endif
