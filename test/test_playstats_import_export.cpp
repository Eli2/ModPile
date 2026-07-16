#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <sqlite3.h>

#include "../src/db/schema/schema.h"
#include "../src/task/import.h"

static sqlite3* open_memory_db() {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	return db;
}

TEST_CASE("playstats import ignores generated rating column from full table export", "[playstats]") {
	sqlite3 *db = open_memory_db();
	REQUIRE(db_migrate(db));
	REQUIRE(sqlite3_exec(db, R"(
		INSERT INTO play(id, rating_total, rating_count, trash, played, skipped, duration)
		VALUES('track-id', 15, 2, 1, 3, 4, 5000)
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	TaskControl tc;
	std::ostringstream out;
	REQUIRE(export_playstats(tc, db, out));

	{
		std::istringstream in(out.str());
		std::string header;
		std::getline(in, header);
		CHECK(header == "id\trating_total\trating_count\trating\ttrash\tplayed\tskipped\tduration");
	}

	REQUIRE(sqlite3_exec(db, "DELETE FROM play", nullptr, nullptr, nullptr) == SQLITE_OK);
	std::istringstream in(out.str());
	REQUIRE(import_playstats(tc, db, in));

	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(db, R"(
		SELECT rating_total, rating_count, rating, trash, played, skipped, duration
		FROM play
		WHERE id = 'track-id'
	)", -1, &stmt, nullptr);
	REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
	CHECK(sqlite3_column_int64(stmt, 0) == 15);
	CHECK(sqlite3_column_int64(stmt, 1) == 2);
	CHECK(sqlite3_column_double(stmt, 2) == 7.5);
	CHECK(sqlite3_column_int64(stmt, 3) == 1);
	CHECK(sqlite3_column_int64(stmt, 4) == 3);
	CHECK(sqlite3_column_int64(stmt, 5) == 4);
	CHECK(sqlite3_column_int64(stmt, 6) == 5000);
	sqlite3_finalize(stmt);

	sqlite3_close(db);
}

TEST_CASE("failed playstats export preserves an existing file", "[playstats]") {
	sqlite3 *db = open_memory_db();
	TaskControl tc;
	const auto path = std::filesystem::temp_directory_path() /
		"modpile-playstats-failed-export.tsv";
	{
		std::ofstream existing(path, std::ios::binary);
		REQUIRE(existing.good());
		existing << "existing contents";
	}

	CHECK_FALSE(export_playstats(tc, db, path));
	CHECK(tc.snapshot().outcome == TaskStatus::Outcome::Failed);
	std::ifstream preserved(path, std::ios::binary);
	REQUIRE(preserved.good());
	CHECK(std::string(std::istreambuf_iterator<char>(preserved), {}) == "existing contents");

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
	sqlite3_close(db);
}

TEST_CASE("successful playstats export replaces its destination", "[playstats]") {
	sqlite3 *db = open_memory_db();
	REQUIRE(db_migrate(db));
	TaskControl tc;
	const auto path = std::filesystem::temp_directory_path() /
		"modpile-playstats-successful-export.tsv";
	{
		std::ofstream existing(path, std::ios::binary);
		REQUIRE(existing.good());
		existing << "old contents";
	}

	REQUIRE(export_playstats(tc, db, path));
	std::ifstream exported(path, std::ios::binary);
	REQUIRE(exported.good());
	const std::string contents(std::istreambuf_iterator<char>(exported), {});
	CHECK(contents.starts_with("id\trating_total\trating_count\trating\t"));

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
	sqlite3_close(db);
}

TEST_CASE("playstats stream wrappers propagate failures", "[playstats]") {
	sqlite3 *db = open_memory_db();
	TaskControl exportControl;
	std::ostringstream out;
	CHECK_FALSE(export_playstats(exportControl, db, out));
	CHECK(exportControl.snapshot().outcome == TaskStatus::Outcome::Failed);

	REQUIRE(db_migrate(db));
	TaskControl importControl;
	std::istringstream in("malformed header\\\n");
	CHECK_FALSE(import_playstats(importControl, db, in));
	CHECK(importControl.snapshot().outcome == TaskStatus::Outcome::Failed);
	sqlite3_close(db);
}
