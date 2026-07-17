#include "settings_store.hpp"

#include "net/json.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace droidcli::cli {
namespace {

core::String hex_encode(const unsigned char* data, const size_t size)
{
	static const char kHexDigits[] = "0123456789abcdef";
	core::String result;
	result.reserve(size * 2);
	for (size_t index = 0; index < size; ++index)
	{
		result += kHexDigits[(data[index] >> 4) & 0xF];
		result += kHexDigits[data[index] & 0xF];
	}
	return result;
}

bool hex_decode(const core::String& hex, core::Array<unsigned char>& out)
{
	if (hex.empty() || hex.size() % 2 != 0)
	{
		return false;
	}
	out.clear();
	out.reserve(hex.size() / 2);
	const auto nibble = [](const char c) -> int
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	for (size_t index = 0; index < hex.size(); index += 2)
	{
		const int high = nibble(hex[index]);
		const int low = nibble(hex[index + 1]);
		if (high < 0 || low < 0)
		{
			return false;
		}
		out.push_back(static_cast<unsigned char>((high << 4) | low));
	}
	return true;
}

#if defined(_WIN32)
// DPAPI (CryptProtectData/CryptUnprotectData) ties the encryption key to the
// current Windows user account - only that account, on that machine, can
// decrypt it back. No password/key management of our own is needed, which
// is the whole point of using it here instead of a hand-rolled cipher.
bool dpapi_protect(const core::String& plaintext, core::String& out_hex)
{
	DATA_BLOB input{};
	input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
	input.cbData = static_cast<DWORD>(plaintext.size());
	DATA_BLOB output{};
	if (!CryptProtectData(&input, L"droidcli settings", nullptr, nullptr, nullptr, 0, &output))
	{
		return false;
	}
	out_hex = hex_encode(output.pbData, output.cbData);
	LocalFree(output.pbData);
	return true;
}

bool dpapi_unprotect(const core::String& hex, core::String& out_plaintext)
{
	core::Array<unsigned char> bytes;
	if (!hex_decode(hex, bytes) || bytes.empty())
	{
		return false;
	}
	DATA_BLOB input{};
	input.pbData = bytes.data();
	input.cbData = static_cast<DWORD>(bytes.size());
	DATA_BLOB output{};
	if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
	{
		return false;
	}
	out_plaintext.assign(reinterpret_cast<char*>(output.pbData), output.cbData);
	LocalFree(output.pbData);
	return true;
}
#endif

// Returns a ready-to-concatenate "key":value JSON field (no leading comma,
// matching net::json_string_field's convention). Empty values are written
// as a plain empty string field - nothing to protect - so a fresh/default
// settings file never triggers the encryption path or its warnings.
core::String serialize_secret_field(const core::String& field_name, const core::String& value)
{
	if (value.empty())
	{
		return net::json_string_field(field_name, "");
	}

#if defined(_WIN32)
	core::String encrypted_hex;
	if (dpapi_protect(value, encrypted_hex))
	{
		return net::json_string_field(field_name + "_dpapi_hex", encrypted_hex);
	}
	std::cerr << "droidcli: DPAPI encryption failed for \"" << field_name
		<< "\" - writing it in plaintext to the settings file." << std::endl;
	return net::json_string_field(field_name, value);
#else
	std::cerr << "droidcli: no at-rest encryption is wired up on this platform - \""
		<< field_name << "\" is being written to the settings file in plaintext. "
		"See \"Config hardening\" in ARCHITECTURE.md." << std::endl;
	return net::json_string_field(field_name, value);
#endif
}

core::String decrypt_secret_field(const core::String& json, const core::String& field_name)
{
	const core::String encrypted_hex = net::extract_json_string_field(json, field_name + "_dpapi_hex");
	if (!encrypted_hex.empty())
	{
#if defined(_WIN32)
		core::String plaintext;
		if (dpapi_unprotect(encrypted_hex, plaintext))
		{
			return plaintext;
		}
		std::cerr << "droidcli: failed to decrypt \"" << field_name << "\" from the settings file "
			"(DPAPI keys are per-user - was this file copied from a different Windows account or machine?)."
			<< std::endl;
		return {};
#else
		std::cerr << "droidcli: settings file has an encrypted \"" << field_name
			<< "\" field but this platform has no DPAPI decryptor - ignoring it." << std::endl;
		return {};
#endif
	}

	// Plaintext fallback - a human hand-editing the file, or a value carried
	// over before the first save_settings() re-encrypts it.
	return net::extract_json_string_field(json, field_name);
}

} // namespace

bool load_settings(const core::String& path, HostSettings& out)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		return false;
	}
	std::ostringstream stream;
	stream << file.rdbuf();
	const core::String json = stream.str();
	if (json.empty())
	{
		return false;
	}

	int64_t numeric_field = 0;
	if (net::extract_json_int_field(json, "port", numeric_field))
	{
		out.port = static_cast<int32_t>(numeric_field);
	}

	bool bool_field = false;
	if (net::extract_json_bool_field(json, "enable_ai", bool_field))
	{
		out.enable_ai = bool_field;
	}
	if (net::extract_json_bool_field(json, "enable_hardware_scan", bool_field))
	{
		out.enable_hardware_scan = bool_field;
	}

	const core::String ollama_url = net::extract_json_string_field(json, "ollama_url");
	if (!ollama_url.empty())
	{
		out.ollama_url = ollama_url;
	}
	const core::String ollama_model = net::extract_json_string_field(json, "ollama_model");
	if (!ollama_model.empty())
	{
		out.ollama_model = ollama_model;
	}
	if (net::extract_json_int_field(json, "ollama_num_ctx", numeric_field))
	{
		out.ollama_num_ctx = static_cast<int32_t>(numeric_field);
	}

	out.api_token = decrypt_secret_field(json, "api_token");

	return true;
}

bool save_settings(const core::String& path, const HostSettings& settings)
{
	core::String json = "{"
		+ core::String("\"port\":") + std::to_string(settings.port) + ","
		+ net::json_bool_field("enable_ai", settings.enable_ai) + ","
		+ net::json_bool_field("enable_hardware_scan", settings.enable_hardware_scan) + ","
		+ net::json_string_field("ollama_url", settings.ollama_url) + ","
		+ net::json_string_field("ollama_model", settings.ollama_model) + ","
		+ core::String("\"ollama_num_ctx\":") + std::to_string(settings.ollama_num_ctx) + ","
		+ serialize_secret_field("api_token", settings.api_token)
		+ "}";

	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		return false;
	}
	file << json;
	return true;
}

} // namespace droidcli::cli
