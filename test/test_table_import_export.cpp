#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
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

	std::string text = "tab\tnewline\ncarriage\rslash\\";
	text += '\0';
	text += "nul";
	text += '\b';
	text += "backspace";
	text += '\f';
	text += "formfeed";
	text += '\v';
	text += "vertical";
	sqlite3_stmt *insert = nullptr;
	sqlite3_prepare_v2(db, "INSERT INTO sample(id, text) VALUES('row', ?1)", -1, &insert, nullptr);
	sqlite3_bind_text(insert, 1, text.data(), text.size(), SQLITE_TRANSIENT);
	REQUIRE(sqlite3_step(insert) == SQLITE_DONE);
	sqlite3_finalize(insert);

	std::ostringstream out;
	REQUIRE(db_export_table(db, "sample", out));
	std::string expected = "id\ttext\nrow\t";
	expected += R"(tab\tnewline\ncarriage\rslash\\\0nul\bbackspace\fformfeed\vvertical)";
	expected += '\n';
	CHECK(out.str() == expected);

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

TEST_CASE("table import export distinguishes null, empty, and literal null marker", "[db][table]") {
	sqlite3 *db = open_memory_db();
	REQUIRE(sqlite3_exec(db, R"(
		CREATE TABLE sample (
			id    TEXT PRIMARY KEY NOT NULL,
			null_value TEXT,
			empty_value TEXT,
			marker_value TEXT
		) STRICT;
		INSERT INTO sample(id, null_value, empty_value, marker_value)
		VALUES('row', NULL, '', '\N');
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	std::ostringstream out;
	REQUIRE(db_export_table(db, "sample", out));
	CHECK(out.str() ==
		"id\tnull_value\tempty_value\tmarker_value\n"
		"row\t\\N\t\t\\\\N\n");

	REQUIRE(sqlite3_exec(db, "DELETE FROM sample", nullptr, nullptr, nullptr) == SQLITE_OK);
	std::istringstream in(out.str());
	REQUIRE(db_import_table(db, "sample", in));

	sqlite3_stmt *select = nullptr;
	REQUIRE(sqlite3_prepare_v2(db,
		"SELECT null_value, empty_value, marker_value FROM sample WHERE id = 'row'",
		-1, &select, nullptr) == SQLITE_OK);
	REQUIRE(sqlite3_step(select) == SQLITE_ROW);
	CHECK(sqlite3_column_type(select, 0) == SQLITE_NULL);
	CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(select, 1))) == "");
	CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(select, 2))) == "\\N");
	sqlite3_finalize(select);

	sqlite3_close(db);
}

TEST_CASE("table import export preserves blob fields", "[db][table]") {
	sqlite3 *db = open_memory_db();
	REQUIRE(sqlite3_exec(db, R"(
		CREATE TABLE sample (
			id         TEXT PRIMARY KEY NOT NULL,
			data       BLOB NOT NULL,
			empty_data BLOB NOT NULL,
			marker     TEXT NOT NULL
		) STRICT;
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	constexpr std::array<std::byte, 6> data = {
		std::byte{0x00}, std::byte{0x01}, std::byte{0x02},
		std::byte{0xfd}, std::byte{0xfe}, std::byte{0xff}
	};
	sqlite3_stmt *insert = nullptr;
	REQUIRE(sqlite3_prepare_v2(db,
		"INSERT INTO sample(id, data, empty_data, marker) VALUES('row', ?1, ?2, '\\BQUJD')",
		-1, &insert, nullptr) == SQLITE_OK);
	sqlite3_bind_blob(insert, 1, data.data(), data.size(), SQLITE_TRANSIENT);
	const char empty_blob = 0;
	sqlite3_bind_blob(insert, 2, &empty_blob, 0, SQLITE_TRANSIENT);
	REQUIRE(sqlite3_step(insert) == SQLITE_DONE);
	sqlite3_finalize(insert);

	std::ostringstream out;
	REQUIRE(db_export_table(db, "sample", out));
	CHECK(out.str() ==
		"id\tdata\tempty_data\tmarker\n"
		"row\t\\BAAEC/f7/\t\\B\t\\\\BQUJD\n");

	REQUIRE(sqlite3_exec(db, "DELETE FROM sample", nullptr, nullptr, nullptr) == SQLITE_OK);
	std::istringstream in(out.str());
	REQUIRE(db_import_table(db, "sample", in));

	sqlite3_stmt *select = nullptr;
	REQUIRE(sqlite3_prepare_v2(db,
		"SELECT data, empty_data, marker FROM sample WHERE id = 'row'",
		-1, &select, nullptr) == SQLITE_OK);
	REQUIRE(sqlite3_step(select) == SQLITE_ROW);
	REQUIRE(sqlite3_column_type(select, 0) == SQLITE_BLOB);
	REQUIRE(sqlite3_column_bytes(select, 0) == static_cast<int>(data.size()));
	CHECK(std::memcmp(sqlite3_column_blob(select, 0), data.data(), data.size()) == 0);
	CHECK(sqlite3_column_type(select, 1) == SQLITE_BLOB);
	CHECK(sqlite3_column_bytes(select, 1) == 0);
	CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(select, 2))) == "\\BQUJD");
	sqlite3_finalize(select);

	sqlite3_close(db);
}

TEST_CASE("table import rejects malformed blob encoding", "[db][table]") {
	sqlite3 *db = open_memory_db();
	REQUIRE(sqlite3_exec(db, "CREATE TABLE sample (id TEXT, data BLOB) STRICT",
		nullptr, nullptr, nullptr) == SQLITE_OK);

	std::istringstream in("id\tdata\nrow\t\\Bnot-base64!\n");
	CHECK_FALSE(db_import_table(db, "sample", in));

	sqlite3_stmt *count = nullptr;
	REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sample", -1, &count, nullptr) == SQLITE_OK);
	REQUIRE(sqlite3_step(count) == SQLITE_ROW);
	CHECK(sqlite3_column_int(count, 0) == 0);
	sqlite3_finalize(count);

	sqlite3_close(db);
}

TEST_CASE("table import rejects ANY columns because storage class is not encoded", "[db][table]") {
	for(const auto *suffix : {"", " STRICT"}) {
		DYNAMIC_SECTION("mode: " << (suffix[0] ? "STRICT ANY" : "non-STRICT ANY")) {
			sqlite3 *db = open_memory_db();
			const auto create = std::string("CREATE TABLE sample (id TEXT, value ANY)") + suffix;
			REQUIRE(sqlite3_exec(db, create.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);

			std::istringstream in("id\tvalue\nrow\t42\n");
			CHECK_FALSE(db_import_table(db, "sample", in));

			sqlite3_stmt *count = nullptr;
			REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sample", -1, &count, nullptr) == SQLITE_OK);
			REQUIRE(sqlite3_step(count) == SQLITE_ROW);
			CHECK(sqlite3_column_int(count, 0) == 0);
			sqlite3_finalize(count);
			sqlite3_close(db);
		}
	}
}
