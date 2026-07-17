#pragma once

#include "core/types.hpp"

// Config hardening (see "Config hardening" in ARCHITECTURE.md): a JSON
// settings file droidcli can load its runtime configuration from, instead
// of every value only ever coming from CLI flags/env vars typed at process
// start. The concrete motivation is a real background service (Phase 33):
// an installed Windows Service's command line is visible to any process on
// the machine (Task Manager, `wmic process`, the Event Log), so a token
// baked into `CreateService`'s command line is a plaintext credential
// exposure a foreground CLI invocation doesn't have. This file exists so
// the service's command line can say "--settings <path>" instead of
// "--token <value>".
namespace droidcli::cli {

struct HostSettings {
	int32_t port = 30080;
	bool enable_ai = true;
	bool enable_hardware_scan = false;
	core::String ollama_url = "http://127.0.0.1:11434";
	core::String ollama_model = "llama3.2";
	int32_t ollama_num_ctx = 32768;
	// Decrypted, in-memory only - never held anywhere but this struct and
	// whatever reads it once at startup.
	core::String api_token;
};

// Loads settings from a JSON file at `path`. Returns false (out untouched)
// if the file doesn't exist or fails to parse - same "absent is fine,
// defaults win" convention as db/droidcli_state.json (cli/droidcli.cpp).
// Secret fields are decrypted via Windows DPAPI when stored as
// "*_dpapi_hex" (see save_settings); a plaintext "api_token" field is also
// honored, for a human hand-editing the file or migrating from a CLI flag -
// the next save_settings() call re-encrypts it.
bool load_settings(const core::String& path, HostSettings& out);

// Writes `settings` to `path` as JSON. On Windows, api_token is encrypted at
// rest via CryptProtectData (DPAPI, scoped to the current Windows user
// account - only that account, on that machine, can decrypt it again) and
// stored as a hex-encoded "api_token_dpapi_hex" field, never plaintext. On a
// non-Windows build (no DPAPI equivalent wired up yet - see "Config
// hardening" in ARCHITECTURE.md) it's written plaintext, with a one-time
// console warning so this isn't a silent gap.
bool save_settings(const core::String& path, const HostSettings& settings);

} // namespace droidcli::cli
