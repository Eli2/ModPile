#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include "../src/db/schema/schema.h"

extern const char* const V001_sql;

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

TEST_CASE("db_migrate succeeds on fresh in-memory database", "[db][migration]") {
	sqlite3 *db = open_memory_db();

	REQUIRE(db_migrate(db) == true);

	SECTION("tracking table records migrations as successful") {
		REQUIRE(migration_count(db) == 3);

		sqlite3_stmt *stmt = nullptr;
		sqlite3_prepare_v2(db,
			"SELECT version, success FROM schema_migration ORDER BY version",
			-1, &stmt, nullptr);
		REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
		CHECK(sqlite3_column_int(stmt, 0) == 1);
		CHECK(sqlite3_column_int(stmt, 1) == 1);
		REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
		CHECK(sqlite3_column_int(stmt, 0) == 2);
		CHECK(sqlite3_column_int(stmt, 1) == 1);
		REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
		CHECK(sqlite3_column_int(stmt, 0) == 3);
		CHECK(sqlite3_column_int(stmt, 1) == 1);
		sqlite3_finalize(stmt);
	}

	SECTION("running db_migrate again is idempotent") {
		REQUIRE(db_migrate(db) == true);
		CHECK(migration_count(db) == 3);
	}

	sqlite3_close(db);
}

TEST_CASE("db_migrate fixes integer division in existing V1 play table", "[db][migration]") {
	sqlite3 *db = open_memory_db();

	REQUIRE(sqlite3_exec(db, V001_sql, nullptr, nullptr, nullptr) == SQLITE_OK);
	REQUIRE(sqlite3_exec(db, R"(
		INSERT INTO play(id, rating_total, rating_count, trash, played, skipped, duration)
		VALUES('track-id', 15, 2, 0, 0, 0, 0);
		INSERT INTO meta(id, md5, todo, file_name, file_size, name, type, bpm, duration, loudness)
		VALUES('track-id', 'md5', 0, 'track.s3m', 123, 'Track', 'S3M', 125, 1000, -14.0)
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	REQUIRE(db_migrate(db) == true);
	CHECK(migration_count(db) == 3);

	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(db,
		"SELECT rating_total, rating_count, rating FROM play WHERE id = 'track-id'",
		-1, &stmt, nullptr);
	REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
	CHECK(sqlite3_column_int64(stmt, 0) == 15);
	CHECK(sqlite3_column_int64(stmt, 1) == 2);
	CHECK(sqlite3_column_double(stmt, 2) == 7.5);
	sqlite3_finalize(stmt);

	sqlite3_prepare_v2(db,
		"SELECT duration, loudness, audible_duration FROM meta WHERE id = 'track-id'",
		-1, &stmt, nullptr);
	REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
	CHECK(sqlite3_column_int64(stmt, 0) == 1000);
	CHECK(sqlite3_column_double(stmt, 1) == -14.0);
	CHECK(sqlite3_column_type(stmt, 2) == SQLITE_NULL);
	sqlite3_finalize(stmt);

	sqlite3_close(db);
}

TEST_CASE("db_migrate aborts when schema_migration version cannot be read", "[db][migration]") {
	sqlite3 *db = open_memory_db();

	REQUIRE(sqlite3_exec(db, R"(
		CREATE TABLE schema_migration (
			broken INTEGER NOT NULL
		)
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	CHECK(db_migrate(db) == false);

	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(db,
		"SELECT COUNT(*) FROM sqlite_schema WHERE type = 'table' AND name = 'file'",
		-1, &stmt, nullptr);
	REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
	CHECK(sqlite3_column_int(stmt, 0) == 0);
	sqlite3_finalize(stmt);

	sqlite3_close(db);
}

TEST_CASE("db_migrate rejects a database from a newer application version", "[db][migration]") {
	sqlite3 *db = open_memory_db();

	REQUIRE(sqlite3_exec(db, R"(
		CREATE TABLE schema_migration (
			version        INTEGER PRIMARY KEY NOT NULL,
			description    TEXT                NOT NULL,
			installed_on   INTEGER             NOT NULL,
			execution_ms   INTEGER             NOT NULL,
			success        INTEGER             NOT NULL
		);
		INSERT INTO schema_migration(version, description, installed_on, execution_ms, success)
		VALUES(999, 'Future schema', 0, 0, 1);
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	CHECK_FALSE(db_migrate(db));
	CHECK(migration_count(db) == 1);

	// Rejecting the database must happen before this binary applies or creates
	// any of its own schema objects.
	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(db,
		"SELECT COUNT(*) FROM sqlite_schema WHERE type = 'table' AND name = 'file'",
		-1, &stmt, nullptr);
	REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
	CHECK(sqlite3_column_int(stmt, 0) == 0);
	sqlite3_finalize(stmt);

	sqlite3_close(db);
}
