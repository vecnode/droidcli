#pragma once

#include "droidcli_core.h"

struct sqlite3;

namespace droidcli::cli {

// One persisted message in an agent-turn session. Mirrors ai::ChatMessage's
// role/content shape plus the bookkeeping (session_id, hop_index,
// created_at) that makes a session resumable and queryable.
struct MemoryRecord {
	core::String session_id;
	int32_t hop_index = 0;
	core::String role;    // "system" | "user" | "assistant" | "tool"
	core::String content;
	core::String created_at;
};

// droidcli's Core-tier "memory" role (see ARCHITECTURE.md's crate
// comparison to ZeroClaw's zeroclaw-memory / zeroclaw-infra): a SQLite-
// backed, append-only log of every message DroidHost::agent_turn adds to a
// session's transcript, so a session survives a process restart and can be
// inspected (GET /api/agent/history, GET /api/agent/sessions) instead of
// living only in an in-memory std::vector.
//
// Lives in cli/ (host), not src/ (core) - this is real file I/O, a host
// concern per AGENTS.md's Golden rule. Compiled straight into the droidcli
// executable target, linked against the vendored SQLite amalgamation (see
// third_party/README.md), never into the portable droidcli_core library.
//
// Deliberately minimal: no embeddings, no vector retrieval, no eviction
// policy - durability and queryability only. See the "Persistent memory"
// phase in ARCHITECTURE.md's extension plan for what's intentionally out of
// scope here.
class MemoryStore {
public:
	MemoryStore() = default;
	~MemoryStore();

	MemoryStore(const MemoryStore&) = delete;
	MemoryStore& operator=(const MemoryStore&) = delete;

	// Opens (creating if necessary) the SQLite database at db_path and
	// ensures the schema exists. Safe to call once at startup; a failure
	// (bad path, disk full, corrupt file) leaves the store closed - append/
	// load calls become no-ops rather than crashing, so a memory-store
	// problem never takes down the rest of the daemon.
	bool open(const core::String& db_path);

	bool is_open() const;

	// Appends one message to session_id's history. No-op (returns false)
	// if the store isn't open.
	bool append(const core::String& session_id, int32_t hop_index, const core::String& role, const core::String& content);

	// Loads every record for session_id, ordered by hop_index. Empty if the
	// store isn't open or the session has no history.
	core::Array<MemoryRecord> load_session(const core::String& session_id) const;

	// Every distinct session_id that has at least one record, most
	// recently active first.
	core::Array<core::String> list_session_ids() const;

private:
	sqlite3* db_ = nullptr;
};

} // namespace droidcli::cli
