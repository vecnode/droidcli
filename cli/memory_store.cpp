#include "memory_store.hpp"

#include "sqlite3.h"

#include <cctype>
#include <ctime>

namespace droidcli::cli {
namespace {

core::String make_timestamp()
{
	const std::time_t now = std::time(nullptr);
	std::tm local_time{};
#ifdef _WIN32
	localtime_s(&local_time, &now);
#else
	localtime_r(&now, &local_time);
#endif
	char buffer[32] = {};
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time);
	return core::String(buffer);
}

// sqlite3_bind_text/column_text take raw C strings; wrapping every call site
// in this pattern keeps the SQLITE_TRANSIENT lifetime rule (SQLite copies
// the string immediately, so a temporary core::String going out of scope
// right after the bind call is safe) explicit at each use.
bool bind_text(sqlite3_stmt* statement, const int index, const core::String& value)
{
	return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

} // namespace

MemoryStore::~MemoryStore()
{
	if (db_ != nullptr)
	{
		sqlite3_close(db_);
		db_ = nullptr;
	}
}

bool MemoryStore::open(const core::String& db_path)
{
	if (db_ != nullptr)
	{
		sqlite3_close(db_);
		db_ = nullptr;
	}

	if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK)
	{
		if (db_ != nullptr)
		{
			sqlite3_close(db_);
			db_ = nullptr;
		}
		return false;
	}

	static const char* kCreateTable =
		"CREATE TABLE IF NOT EXISTS memory_entries ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"session_id TEXT NOT NULL,"
		"hop_index INTEGER NOT NULL,"
		"role TEXT NOT NULL,"
		"content TEXT NOT NULL,"
		"created_at TEXT NOT NULL"
		");"
		"CREATE INDEX IF NOT EXISTS idx_memory_entries_session "
		"ON memory_entries(session_id, hop_index);"
		"CREATE TABLE IF NOT EXISTS command_lessons ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"tool TEXT NOT NULL,"
		"broken TEXT NOT NULL,"
		"failure_reason TEXT NOT NULL,"
		"working TEXT NOT NULL,"
		"lesson TEXT NOT NULL,"
		"created_at TEXT NOT NULL"
		");"
		"CREATE INDEX IF NOT EXISTS idx_command_lessons_tool "
		"ON command_lessons(tool);"
		"CREATE TABLE IF NOT EXISTS known_locations ("
		"name TEXT PRIMARY KEY COLLATE NOCASE,"
		"resolved_path TEXT NOT NULL,"
		"created_at TEXT NOT NULL,"
		"updated_at TEXT NOT NULL"
		");";

	char* error_message = nullptr;
	if (sqlite3_exec(db_, kCreateTable, nullptr, nullptr, &error_message) != SQLITE_OK)
	{
		sqlite3_free(error_message);
		sqlite3_close(db_);
		db_ = nullptr;
		return false;
	}

	return true;
}

bool MemoryStore::is_open() const
{
	return db_ != nullptr;
}

bool MemoryStore::append(
	const core::String& session_id, const int32_t hop_index, const core::String& role, const core::String& content)
{
	if (db_ == nullptr)
	{
		return false;
	}

	static const char* kInsert =
		"INSERT INTO memory_entries (session_id, hop_index, role, content, created_at) "
		"VALUES (?, ?, ?, ?, ?);";

	sqlite3_stmt* statement = nullptr;
	if (sqlite3_prepare_v2(db_, kInsert, -1, &statement, nullptr) != SQLITE_OK)
	{
		return false;
	}

	bool ok = bind_text(statement, 1, session_id)
		&& sqlite3_bind_int(statement, 2, hop_index) == SQLITE_OK
		&& bind_text(statement, 3, role)
		&& bind_text(statement, 4, content)
		&& bind_text(statement, 5, make_timestamp());

	if (ok)
	{
		ok = sqlite3_step(statement) == SQLITE_DONE;
	}

	sqlite3_finalize(statement);
	return ok;
}

core::Array<MemoryRecord> MemoryStore::load_session(const core::String& session_id) const
{
	core::Array<MemoryRecord> records;
	if (db_ == nullptr)
	{
		return records;
	}

	static const char* kSelect =
		"SELECT hop_index, role, content, created_at FROM memory_entries "
		"WHERE session_id = ? ORDER BY hop_index ASC;";

	sqlite3_stmt* statement = nullptr;
	if (sqlite3_prepare_v2(db_, kSelect, -1, &statement, nullptr) != SQLITE_OK)
	{
		return records;
	}

	if (!bind_text(statement, 1, session_id))
	{
		sqlite3_finalize(statement);
		return records;
	}

	while (sqlite3_step(statement) == SQLITE_ROW)
	{
		MemoryRecord record;
		record.session_id = session_id;
		record.hop_index = sqlite3_column_int(statement, 0);
		record.role = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
		record.content = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
		record.created_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
		records.push_back(record);
	}

	sqlite3_finalize(statement);
	return records;
}

core::Array<core::String> MemoryStore::list_session_ids() const
{
	core::Array<core::String> session_ids;
	if (db_ == nullptr)
	{
		return session_ids;
	}

	static const char* kSelect =
		"SELECT session_id, MAX(created_at) AS last_seen FROM memory_entries "
		"GROUP BY session_id ORDER BY last_seen DESC;";

	sqlite3_stmt* statement = nullptr;
	if (sqlite3_prepare_v2(db_, kSelect, -1, &statement, nullptr) != SQLITE_OK)
	{
		return session_ids;
	}

	while (sqlite3_step(statement) == SQLITE_ROW)
	{
		session_ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
	}

	sqlite3_finalize(statement);
	return session_ids;
}

bool MemoryStore::record_lesson(
	const core::String& tool,
	const core::String& broken,
	const core::String& failure_reason,
	const core::String& working,
	const core::String& lesson)
{
	if (db_ == nullptr)
	{
		return false;
	}

	static const char* kInsert =
		"INSERT INTO command_lessons (tool, broken, failure_reason, working, lesson, created_at) "
		"VALUES (?, ?, ?, ?, ?, ?);";

	sqlite3_stmt* statement = nullptr;
	if (sqlite3_prepare_v2(db_, kInsert, -1, &statement, nullptr) != SQLITE_OK)
	{
		return false;
	}

	bool ok = bind_text(statement, 1, tool)
		&& bind_text(statement, 2, broken)
		&& bind_text(statement, 3, failure_reason)
		&& bind_text(statement, 4, working)
		&& bind_text(statement, 5, lesson)
		&& bind_text(statement, 6, make_timestamp());

	if (ok)
	{
		ok = sqlite3_step(statement) == SQLITE_DONE;
	}

	sqlite3_finalize(statement);
	return ok;
}

core::Array<CommandLesson> MemoryStore::search_lessons(const core::String& query, const int32_t max_results) const
{
	core::Array<CommandLesson> lessons;
	if (db_ == nullptr)
	{
		return lessons;
	}

	static const char* kSelect =
		"SELECT id, tool, broken, failure_reason, working, lesson, created_at FROM command_lessons "
		"WHERE LOWER(tool) LIKE ?1 OR LOWER(broken) LIKE ?1 OR LOWER(failure_reason) LIKE ?1 OR LOWER(lesson) LIKE ?1 "
		"ORDER BY created_at DESC LIMIT ?2;";

	sqlite3_stmt* statement = nullptr;
	if (sqlite3_prepare_v2(db_, kSelect, -1, &statement, nullptr) != SQLITE_OK)
	{
		return lessons;
	}

	core::String lowered_query;
	lowered_query.reserve(query.size());
	for (const unsigned char character : query)
	{
		lowered_query += static_cast<char>(std::tolower(character));
	}
	const core::String pattern = "%" + lowered_query + "%";

	if (!bind_text(statement, 1, pattern) || sqlite3_bind_int(statement, 2, max_results) != SQLITE_OK)
	{
		sqlite3_finalize(statement);
		return lessons;
	}

	while (sqlite3_step(statement) == SQLITE_ROW)
	{
		CommandLesson entry;
		entry.id = sqlite3_column_int64(statement, 0);
		entry.tool = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
		entry.broken = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
		entry.failure_reason = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
		entry.working = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
		entry.lesson = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
		entry.created_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 6));
		lessons.push_back(entry);
	}

	sqlite3_finalize(statement);
	return lessons;
}

bool MemoryStore::remember_location(const core::String& name, const core::String& resolved_path)
{
	if (db_ == nullptr)
	{
		return false;
	}

	// ON CONFLICT upsert keyed on name (COLLATE NOCASE, from the table
	// definition) - remembering the same name again updates resolved_path/
	// updated_at in place instead of growing a duplicate row, and preserves
	// the original created_at.
	static const char* kUpsert =
		"INSERT INTO known_locations (name, resolved_path, created_at, updated_at) "
		"VALUES (?, ?, ?, ?) "
		"ON CONFLICT(name) DO UPDATE SET resolved_path = excluded.resolved_path, updated_at = excluded.updated_at;";

	sqlite3_stmt* statement = nullptr;
	if (sqlite3_prepare_v2(db_, kUpsert, -1, &statement, nullptr) != SQLITE_OK)
	{
		return false;
	}

	const core::String now = make_timestamp();
	bool ok = bind_text(statement, 1, name)
		&& bind_text(statement, 2, resolved_path)
		&& bind_text(statement, 3, now)
		&& bind_text(statement, 4, now);

	if (ok)
	{
		ok = sqlite3_step(statement) == SQLITE_DONE;
	}

	sqlite3_finalize(statement);
	return ok;
}

core::Array<KnownLocation> MemoryStore::list_locations() const
{
	core::Array<KnownLocation> locations;
	if (db_ == nullptr)
	{
		return locations;
	}

	static const char* kSelect =
		"SELECT name, resolved_path, created_at, updated_at FROM known_locations "
		"ORDER BY updated_at DESC;";

	sqlite3_stmt* statement = nullptr;
	if (sqlite3_prepare_v2(db_, kSelect, -1, &statement, nullptr) != SQLITE_OK)
	{
		return locations;
	}

	while (sqlite3_step(statement) == SQLITE_ROW)
	{
		KnownLocation location;
		location.name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
		location.resolved_path = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
		location.created_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
		location.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
		locations.push_back(location);
	}

	sqlite3_finalize(statement);
	return locations;
}

} // namespace droidcli::cli
