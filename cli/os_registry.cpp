#include "os_registry.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#endif

namespace droidcli::cli {

#ifdef _WIN32

namespace {

HKEY to_hkey(const RegistryRoot root)
{
	return root == RegistryRoot::CurrentUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

} // namespace

core::String read_registry_string(
	const RegistryRoot root,
	const core::String& subkey,
	const core::String& value_name)
{
	HKEY key = nullptr;
	if (RegOpenKeyExA(to_hkey(root), subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
	{
		return {};
	}

	const char* value_name_ptr = value_name.empty() ? nullptr : value_name.c_str();
	DWORD type = 0;
	DWORD size = 0;
	if (RegQueryValueExA(key, value_name_ptr, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0)
	{
		RegCloseKey(key);
		return {};
	}
	if (type != REG_SZ && type != REG_EXPAND_SZ)
	{
		RegCloseKey(key);
		return {};
	}

	std::vector<char> buffer(static_cast<size_t>(size) + 1, '\0');
	const LONG query_result = RegQueryValueExA(
		key, value_name_ptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
	RegCloseKey(key);
	if (query_result != ERROR_SUCCESS)
	{
		return {};
	}

	return core::String(buffer.data());
}

bool read_registry_dword(
	const RegistryRoot root,
	const core::String& subkey,
	const core::String& value_name,
	uint32_t& out_value)
{
	HKEY key = nullptr;
	if (RegOpenKeyExA(to_hkey(root), subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
	{
		return false;
	}

	DWORD type = 0;
	DWORD dword_value = 0;
	DWORD size = sizeof(dword_value);
	const LONG query_result = RegQueryValueExA(
		key, value_name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&dword_value), &size);
	RegCloseKey(key);
	if (query_result != ERROR_SUCCESS || type != REG_DWORD)
	{
		return false;
	}

	out_value = static_cast<uint32_t>(dword_value);
	return true;
}

#else // POSIX - no registry, always fail closed.

core::String read_registry_string(RegistryRoot, const core::String&, const core::String&)
{
	return {};
}

bool read_registry_dword(RegistryRoot, const core::String&, const core::String&, uint32_t&)
{
	return false;
}

#endif

} // namespace droidcli::cli
