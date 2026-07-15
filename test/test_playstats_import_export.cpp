#include <catch2/catch_test_macros.hpp>

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
	export_playstats(tc, db, out);

	{
		std::istringstream in(out.str());
		std::string header;
		std::getline(in, header);
		CHECK(header == "id\trating_total\trating_count\trating\ttrash\tplayed\tskipped\tduration");
	}

	REQUIRE(sqlite3_exec(db, "DELETE FROM play", nullptr, nullptr, nullptr) == SQLITE_OK);
	std::istringstream in(out.str());
	import_playstats(tc, db, in);

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
