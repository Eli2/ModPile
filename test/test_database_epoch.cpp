// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

#include <sqlite3.h>

#include "db.h"
#include "db/database_epoch.h"
#include "db/epoch/epoch.h"
#include "db/schema/schema.h"

namespace {

struct TemporaryDatabase {
	std::filesystem::path path = std::filesystem::temp_directory_path()
		/ ("modpile-epoch-test-" + std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");

	~TemporaryDatabase() {
		std::error_code error;
		std::filesystem::remove(path, error);
		std::filesystem::remove(path.string() + "-wal", error);
		std::filesystem::remove(path.string() + "-shm", error);
	}
};

int schema_object_count(sqlite3 *db) {
	sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db,
		"SELECT COUNT(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'",
		-1, &stmt, nullptr) != SQLITE_OK) {
		return -1;
	}
	const int count = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
	sqlite3_finalize(stmt);
	return count;
}

} // namespace

TEST_CASE("application version families have explicit database epochs", "[db][epoch]") {
	CHECK(database_epoch_for_app_version(0, 0) == 0);
	CHECK(database_epoch_for_app_version(0, 1) == 0);
	CHECK_FALSE(database_epoch_for_app_version(0, 2).has_value());
	CHECK_FALSE(database_epoch_for_app_version(1, 0).has_value());

	CHECK(MODPILE_DATABASE_EPOCH == 0);
}

TEST_CASE("database epoch compatibility requires exact equality", "[db][epoch]") {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

	int actual_epoch = -1;
	CHECK(db_check_database_epoch(db, 0, actual_epoch) == DatabaseEpochCheck::compatible);
	CHECK(actual_epoch == 0);

	REQUIRE(db_set_database_epoch(db, 2));
	CHECK(db_check_database_epoch(db, 2, actual_epoch) == DatabaseEpochCheck::compatible);
	CHECK(actual_epoch == 2);
	CHECK(db_check_database_epoch(db, 1, actual_epoch) == DatabaseEpochCheck::incompatible);
	CHECK(actual_epoch == 2);
	CHECK(db_check_database_epoch(db, 3, actual_epoch) == DatabaseEpochCheck::incompatible);
	CHECK(actual_epoch == 2);

	CHECK(sqlite3_close(db) == SQLITE_OK);
}

TEST_CASE("negative database epochs cannot be written", "[db][epoch]") {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	CHECK_FALSE(db_set_database_epoch(db, -1));
	CHECK(sqlite3_close(db) == SQLITE_OK);
}

TEST_CASE("epoch 0 schema validation accepts the canonical migrated schema", "[db][epoch][schema]") {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	REQUIRE(db_migrate(db));

	std::string error;
	CHECK(db_validate_epoch_schema(db, 0, error));
	CHECK(error.empty());
	CHECK(sqlite3_close(db) == SQLITE_OK);
}

TEST_CASE("epoch 0 schema validation rejects altered or missing expected tables", "[db][epoch][schema]") {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	REQUIRE(db_migrate(db));

	SECTION("altered table") {
		REQUIRE(sqlite3_exec(db, "ALTER TABLE meta ADD COLUMN unexpected TEXT", nullptr, nullptr, nullptr) == SQLITE_OK);
	}
	SECTION("missing table") {
		REQUIRE(sqlite3_exec(db, "DROP TABLE modland_meta", nullptr, nullptr, nullptr) == SQLITE_OK);
	}

	std::string error;
	CHECK_FALSE(db_validate_epoch_schema(db, 0, error));
	CHECK_FALSE(error.empty());
	CHECK(sqlite3_close(db) == SQLITE_OK);
}

TEST_CASE("epoch schema validation permits unrelated additional tables", "[db][epoch][schema]") {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	REQUIRE(db_migrate(db));
	REQUIRE(sqlite3_exec(db, "CREATE TABLE unrelated(id INTEGER)", nullptr, nullptr, nullptr) == SQLITE_OK);

	std::string error;
	CHECK(db_validate_epoch_schema(db, 0, error));
	CHECK(error.empty());
	CHECK(sqlite3_close(db) == SQLITE_OK);
}

TEST_CASE("unknown database epochs have no schema validator", "[db][epoch][schema]") {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

	std::string error;
	CHECK_FALSE(db_validate_epoch_schema(db, 999, error));
	CHECK(error == "Unsupported database epoch: 999");
	CHECK(sqlite3_close(db) == SQLITE_OK);
}

TEST_CASE("db_init rejects a different epoch before applying migrations", "[db][epoch]") {
	TemporaryDatabase temporary;

	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(temporary.path.c_str(), &db) == SQLITE_OK);
	// fourcc('M', 'P', 'L', 'E')
	REQUIRE(sqlite3_exec(db, "PRAGMA application_id = 1297108037", nullptr, nullptr, nullptr) == SQLITE_OK);
	REQUIRE(db_set_database_epoch(db, 1));
	REQUIRE(schema_object_count(db) == 0);
	REQUIRE(sqlite3_close(db) == SQLITE_OK);

	const auto result = db_init(temporary.path);
	CHECK_FALSE(result.success);
	CHECK(result.error_message.find("incompatible format epoch 1") != std::string::npos);

	// The epoch gate must run before db_migrate creates schema_migration or any
	// application tables.
	REQUIRE(sqlite3_open(temporary.path.c_str(), &db) == SQLITE_OK);
	CHECK(schema_object_count(db) == 0);
	int actual_epoch = -1;
	CHECK(db_check_database_epoch(db, 1, actual_epoch) == DatabaseEpochCheck::compatible);
	CHECK(sqlite3_close(db) == SQLITE_OK);
}
