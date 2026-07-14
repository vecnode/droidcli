#pragma once

#if defined(_WIN32) && defined(DROIDCLI_BUILDING_SHARED)
#if defined(DROIDCLI_BUILDING)
#define DROIDCLI_API __declspec(dllexport)
#else
#define DROIDCLI_API __declspec(dllimport)
#endif
#else
#define DROIDCLI_API
#endif
