// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <zstd.h>

#include "../src/db/schema/schema.h"
#include "../src/task/export.h"

namespace {

struct TempDirectory {
	std::filesystem::path path = std::filesystem::temp_directory_path()
		/ ("modpile-playlist-export-test-" + std::to_string(reinterpret_cast<uintptr_t>(this)));

	TempDirectory() {
		REQUIRE(std::filesystem::create_directory(path));
	}

	~TempDirectory() {
		std::error_code ignored;
		std::filesystem::remove_all(path, ignored);
	}
};

sqlite3 *make_database() {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	REQUIRE(db_migrate(db));
	return db;
}

void insert_file(sqlite3 *db, const std::string &id, const std::string &name,
		const std::vector<std::byte> &raw) {
	std::vector<std::byte> compressed(ZSTD_compressBound(raw.size()));
	const auto compressed_size = ZSTD_compress(
		compressed.data(), compressed.size(), raw.data(), raw.size(), 1);
	REQUIRE_FALSE(ZSTD_isError(compressed_size));
	compressed.resize(compressed_size);

	sqlite3_stmt *stmt = nullptr;
	REQUIRE(sqlite3_prepare_v2(db,
		"INSERT INTO file(id,name,size,data) VALUES(?1,?2,?3,?4)",
		-1, &stmt, nullptr) == SQLITE_OK);
	sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(raw.size()));
	sqlite3_bind_blob64(stmt, 4, compressed.data(), compressed.size(), SQLITE_TRANSIENT);
	REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
	sqlite3_finalize(stmt);
}

} // namespace

TEST_CASE("playlist export fails when a track cannot be read", "[playlist-export]") {
	TempDirectory output;
	sqlite3 *db = make_database();
	TaskControl tc;
	const std::vector<ExportTrack> tracks{{"missing", "missing.mod", "", ""}};

	CHECK_FALSE(export_playlist_run(tc, db, output.path, "Missing", tracks));
	CHECK(tc.snapshot().outcome == TaskStatus::Outcome::Failed);
	CHECK_FALSE(std::filesystem::exists(output.path / "Missing.xspf"));
	sqlite3_close(db);
}

TEST_CASE("playlist export fails when a track destination cannot be opened", "[playlist-export]") {
	TempDirectory output;
	const auto not_a_directory = output.path / "file";
	{
		std::ofstream marker(not_a_directory);
		REQUIRE(marker);
	}

	sqlite3 *db = make_database();
	insert_file(db, "track", "track.mod", {std::byte{1}, std::byte{2}});
	TaskControl tc;
	const std::vector<ExportTrack> tracks{{"track", "track.mod", "", ""}};

	CHECK_FALSE(export_playlist_run(tc, db, not_a_directory, "Blocked", tracks));
	CHECK(tc.snapshot().outcome == TaskStatus::Outcome::Failed);
	sqlite3_close(db);
}

TEST_CASE("playlist export fails when the XSPF destination cannot be opened", "[playlist-export]") {
	TempDirectory output;
	REQUIRE(std::filesystem::create_directory(output.path / "Blocked.xspf"));
	sqlite3 *db = make_database();
	TaskControl tc;

	CHECK_FALSE(export_playlist_run(tc, db, output.path, "Blocked", {}));
	CHECK(tc.snapshot().outcome == TaskStatus::Outcome::Failed);
	sqlite3_close(db);
}
