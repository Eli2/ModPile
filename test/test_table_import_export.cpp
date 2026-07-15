#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include <sqlite3.h>

#include "../src/db/table/table_import_export.h"

static sqlite3* open_memory_db() {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	return db;
}

TEST_CASE("table import export escapes row and column separators", "[db][table]") {
	sqlite3 *db = open_memory_db();
	REQUIRE(sqlite3_exec(db, R"(
		CREATE TABLE sample (
			id   TEXT PRIMARY KEY NOT NULL,
			text TEXT
		) STRICT;
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	const std::string text = "tab\tnewline\ncarriage\rslash\\done";
	sqlite3_stmt *insert = nullptr;
	sqlite3_prepare_v2(db, "INSERT INTO sample(id, text) VALUES('row', ?1)", -1, &insert, nullptr);
	sqlite3_bind_text(insert, 1, text.data(), text.size(), SQLITE_TRANSIENT);
	REQUIRE(sqlite3_step(insert) == SQLITE_DONE);
	sqlite3_finalize(insert);

	std::ostringstream out;
	REQUIRE(db_export_table(db, "sample", out));
	CHECK(out.str() == "id\ttext\nrow\ttab\\tnewline\\ncarriage\\rslash\\\\done\n");

	REQUIRE(sqlite3_exec(db, "DELETE FROM sample", nullptr, nullptr, nullptr) == SQLITE_OK);
	std::istringstream in(out.str());
	REQUIRE(db_import_table(db, "sample", in));

	sqlite3_stmt *select = nullptr;
	sqlite3_prepare_v2(db, "SELECT text FROM sample WHERE id = 'row'", -1, &select, nullptr);
	REQUIRE(sqlite3_step(select) == SQLITE_ROW);
	CHECK(reinterpret_cast<const char*>(sqlite3_column_text(select, 0)) == text);
	sqlite3_finalize(select);

	sqlite3_close(db);
}

TEST_CASE("table import keeps trailing empty fields", "[db][table]") {
	sqlite3 *db = open_memory_db();
	REQUIRE(sqlite3_exec(db, R"(
		CREATE TABLE sample (
			id   TEXT PRIMARY KEY NOT NULL,
			a    TEXT,
			b    TEXT
		) STRICT;
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	std::istringstream in("id\ta\tb\r\nrow\tvalue\t\r\n");
	REQUIRE(db_import_table(db, "sample", in));

	sqlite3_stmt *select = nullptr;
	sqlite3_prepare_v2(db, "SELECT a, b FROM sample WHERE id = 'row'", -1, &select, nullptr);
	REQUIRE(sqlite3_step(select) == SQLITE_ROW);
	CHECK(reinterpret_cast<const char*>(sqlite3_column_text(select, 0)) == std::string("value"));
	CHECK(reinterpret_cast<const char*>(sqlite3_column_text(select, 1)) == std::string(""));
	sqlite3_finalize(select);

	sqlite3_close(db);
}

TEST_CASE("table import export preserves utf-8 text", "[db][table]") {
	sqlite3 *db = open_memory_db();
	REQUIRE(sqlite3_exec(db, R"(
		CREATE TABLE sample (
			id   TEXT PRIMARY KEY NOT NULL,
			text TEXT
		) STRICT;
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	const std::string text = "Grüße 日本語 🎛️\tline\n";
	sqlite3_stmt *insert = nullptr;
	sqlite3_prepare_v2(db, "INSERT INTO sample(id, text) VALUES('row', ?1)", -1, &insert, nullptr);
	sqlite3_bind_text(insert, 1, text.data(), text.size(), SQLITE_TRANSIENT);
	REQUIRE(sqlite3_step(insert) == SQLITE_DONE);
	sqlite3_finalize(insert);

	std::ostringstream out;
	REQUIRE(db_export_table(db, "sample", out));

	REQUIRE(sqlite3_exec(db, "DELETE FROM sample", nullptr, nullptr, nullptr) == SQLITE_OK);
	std::istringstream in(out.str());
	REQUIRE(db_import_table(db, "sample", in));

	sqlite3_stmt *select = nullptr;
	sqlite3_prepare_v2(db, "SELECT text FROM sample WHERE id = 'row'", -1, &select, nullptr);
	REQUIRE(sqlite3_step(select) == SQLITE_ROW);
	const auto *value = reinterpret_cast<const char*>(sqlite3_column_text(select, 0));
	const auto nbytes = static_cast<size_t>(sqlite3_column_bytes(select, 0));
	CHECK(std::string(value, nbytes) == text);
	sqlite3_finalize(select);

	sqlite3_close(db);
}
