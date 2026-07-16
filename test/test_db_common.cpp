#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include <sqlite3.h>
#include <zstd.h>

#include "../src/db_common.h"

TEST_CASE("db_get_file treats the stored decompressed size as a hint", "[db][zstd]") {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	REQUIRE(sqlite3_exec(db, R"(
		CREATE TABLE file (
			id   TEXT PRIMARY KEY NOT NULL,
			name TEXT             NOT NULL,
			size INTEGER          NOT NULL,
			data BLOB             NOT NULL
		)
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	constexpr std::array<std::byte, 4> input = {
		std::byte{'M'}, std::byte{'O'}, std::byte{'D'}, std::byte{'!'}
	};
	std::vector<std::byte> compressed(ZSTD_compressBound(input.size()));
	const auto compressed_size = ZSTD_compress(
		compressed.data(), compressed.size(), input.data(), input.size(), 1);
	REQUIRE_FALSE(ZSTD_isError(compressed_size));
	compressed.resize(compressed_size);

	sqlite3_stmt *insert = nullptr;
	REQUIRE(sqlite3_prepare_v2(db,
		"INSERT INTO file(id, name, size, data) VALUES('track', 'track.mod', ?1, ?2)",
		-1, &insert, nullptr) == SQLITE_OK);
	const auto stored_size = GENERATE(int64_t{2}, int64_t{8});
	CAPTURE(stored_size);
	// The frame expands to four bytes; exercise both an undersized and oversized
	// database hint.
	sqlite3_bind_int64(insert, 1, stored_size);
	sqlite3_bind_blob(insert, 2, compressed.data(), static_cast<int>(compressed.size()), SQLITE_TRANSIENT);
	REQUIRE(sqlite3_step(insert) == SQLITE_DONE);
	sqlite3_finalize(insert);

	FileRow file;
	REQUIRE(db_get_file(db, "track", file));
	CHECK(file.id == "track");
	CHECK(file.name == "track.mod");
	REQUIRE(file.rawData.size() == input.size());
	CHECK(std::equal(file.rawData.begin(), file.rawData.end(), input.begin()));

	sqlite3_close(db);
}
