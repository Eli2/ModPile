#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "../src/db/schema/schema.h"

static sqlite3* open_memory_db() {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	return db;
}

static int migration_count(sqlite3 *db) {
	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM schema_migration WHERE success = 1", -1, &stmt, nullptr);
	int count = 0;
	if(sqlite3_step(stmt) == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
	}
	sqlite3_finalize(stmt);
	return count;
}

// ---------------------------------------------------------------------------

struct ColSpec { const char *name; const char *type; };

// Checks that the expected columns exist in order.
// Use xinfo=false for virtual tables (e.g. FTS5) to avoid SQLite-internal hidden columns.
static void check_columns(sqlite3 *db, const char *table, std::initializer_list<ColSpec> expected, bool xinfo = true) {
	auto sql = std::string(xinfo ? "PRAGMA table_xinfo(" : "PRAGMA table_info(") + table + ")";
	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

	std::vector<std::pair<std::string, std::string>> actual;
	while(sqlite3_step(stmt) == SQLITE_ROW) {
		std::string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
		std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
		actual.emplace_back(name, type);
	}
	sqlite3_finalize(stmt);

	REQUIRE(actual.size() == expected.size());
	int i = 0;
	for(auto &exp : expected) {
		INFO("column " << i << " of table " << table);
		CHECK(actual[i].first  == exp.name);
		CHECK(actual[i].second == exp.type);
		i++;
	}
}

// ---------------------------------------------------------------------------

TEST_CASE("db_migrate succeeds on fresh in-memory database", "[db][migration]") {
	sqlite3 *db = open_memory_db();

	REQUIRE(db_migrate(db) == true);

	SECTION("tracking table records V1 as successful") {
		REQUIRE(migration_count(db) == 1);

		sqlite3_stmt *stmt = nullptr;
		sqlite3_prepare_v2(db,
			"SELECT version, success FROM schema_migration ORDER BY version",
			-1, &stmt, nullptr);
		REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
		CHECK(sqlite3_column_int(stmt, 0) == 1);
		CHECK(sqlite3_column_int(stmt, 1) == 1);
		sqlite3_finalize(stmt);
	}

	SECTION("schema matches expected column definitions") {
		check_columns(db, "file", {
			{ "id",   "TEXT"    },
			{ "name", "TEXT"    },
			{ "size", "INTEGER" },
			{ "data", "BLOB"    },
		});

		check_columns(db, "meta", {
			{ "id",        "TEXT"    },
			{ "md5",       "TEXT"    },
			{ "todo",      "INTEGER" },
			{ "file_name", "TEXT"    },
			{ "file_size", "INTEGER" },
			{ "name",      "TEXT"    },
			{ "type",      "TEXT"    },
			{ "bpm",       "INTEGER" },
			{ "duration",  "INTEGER" },
			{ "loudness",  "REAL"    },
		});

		check_columns(db, "modland", {
			{ "md5",    "TEXT" },
			{ "format", "TEXT" },
			{ "artist", "TEXT" },
			{ "name",   "TEXT" },
			{ "path",   "TEXT" },
		});

		check_columns(db, "modland_meta", {
			{ "md5", "TEXT"    },
			{ "bad", "INTEGER" },
		});

		check_columns(db, "modland_format", {
			{ "format",        "TEXT"    },
			{ "xmp_supported", "INTEGER" },
		});

		check_columns(db, "play", {
			{ "id",           "TEXT"    },
			{ "rating_total", "INTEGER" },
			{ "rating_count", "INTEGER" },
			{ "rating",       "REAL"    },
			{ "trash",        "INTEGER" },
			{ "played",       "INTEGER" },
			{ "skipped",      "INTEGER" },
			{ "duration",     "INTEGER" },
		});

		check_columns(db, "text_index", {
			{ "id",        "" },
			{ "file_name", "" },
			{ "name",      "" },
			{ "artist",    "" },
		}, false);

		check_columns(db, "playlist", {
			{ "id",          "INTEGER" },
			{ "name",        "TEXT"    },
			{ "description", "TEXT"    },
		});

		check_columns(db, "playlist_track", {
			{ "id",          "INTEGER" },
			{ "playlist_id", "INTEGER" },
			{ "track_id",    "TEXT"    },
			{ "track_order", "INTEGER" },
		});
	}

	SECTION("running db_migrate again is idempotent") {
		REQUIRE(db_migrate(db) == true);
		CHECK(migration_count(db) == 1);
	}

	sqlite3_close(db);
}
