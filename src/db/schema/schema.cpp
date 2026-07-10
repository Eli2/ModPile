// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "schema.h"

#include <chrono>
#include <format>
#include <string>

#include <sqlite3.h>

#include "../../log.h"

// ---------------------------------------------------------------------------
// Migration table bootstrap
// ---------------------------------------------------------------------------

static const char *kCreateMigrationTable = R"(
	CREATE TABLE IF NOT EXISTS schema_migration (
		version        INTEGER  PRIMARY KEY NOT NULL,
		description    TEXT                 NOT NULL,
		installed_on   INTEGER              NOT NULL,
		execution_ms   INTEGER              NOT NULL,
		success        INTEGER              NOT NULL
	)
	;
)";

// ---------------------------------------------------------------------------
// Migration definitions
// Each VNN_*.cpp file defines one `extern const char* const VNN_sql` symbol.
// ---------------------------------------------------------------------------

extern const char* const V001_sql;

struct Migration {
	int         version;
	const char *description;
	const char *sql;
};

static const Migration kMigrations[] = {
	{ 1, "Initial schema", V001_sql },
	// Add new migrations here:
	// { 2, "Short description", V002_sql },
};

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

static int64_t now_ms() {
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static int current_version(sqlite3 *db) {
	const char *sql = "SELECT COALESCE(MAX(version), 0) FROM schema_migration WHERE success = 1";
	sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return 0;
	}
	
	int v = 0;
	if(sqlite3_step(stmt) == SQLITE_ROW) {
		v = sqlite3_column_int(stmt, 0);
	}
	sqlite3_finalize(stmt);
	return v;
}

static bool record_migration(sqlite3 *db, const Migration &m, int64_t exec_ms, bool success) {
	const char *sql = R"(
		INSERT INTO schema_migration(version, description, installed_on, execution_ms, success)
		VALUES(?1, ?2, ?3, ?4, ?5)
	)";
	sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		log_error("schema_migration record prepare failed: {}", sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int (stmt, 1, m.version);
	sqlite3_bind_text(stmt, 2, m.description, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 3, now_ms());
	sqlite3_bind_int64(stmt, 4, exec_ms);
	sqlite3_bind_int (stmt, 5, success ? 1 : 0);
	bool ok = sqlite3_step(stmt) == SQLITE_DONE;
	sqlite3_finalize(stmt);
	return ok;
}

bool db_migrate(sqlite3 *db) {
	// Bootstrap: ensure the tracking table exists (outside any migration transaction)
	char *errmsg = nullptr;
	if(sqlite3_exec(db, kCreateMigrationTable, nullptr, nullptr, &errmsg) != SQLITE_OK) {
		log_error("Failed to create schema_migration table: {}", errmsg);
		sqlite3_free(errmsg);
		return false;
	}

	const int version = current_version(db);

	for(const auto &m : kMigrations) {
		if(m.version <= version)
			continue;

		log_info("Applying migration V{}: {}", m.version, m.description);

		sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);

		const int64_t t0 = now_ms();
		errmsg = nullptr;
		const int rc = sqlite3_exec(db, m.sql, nullptr, nullptr, &errmsg);
		const int64_t elapsed = now_ms() - t0;

		if(rc != SQLITE_OK) {
			log_error("Migration V{} failed: {}", m.version, errmsg ? errmsg : "unknown");
			sqlite3_free(errmsg);
			record_migration(db, m, elapsed, false);
			sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
			return false;
		}

		record_migration(db, m, elapsed, true);
		sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

		log_info("Migration V{} applied in {} ms", m.version, elapsed);
	}

	return true;
}
