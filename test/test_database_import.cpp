#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <zstd.h>

#include "../src/db.h"
#include "../src/db/database_epoch.h"
#include "../src/db/epoch/epoch.h"
#include "../src/db/schema/schema.h"
#include "../src/task/db_import/db_import.h"
#include "../src/util/hash_util.h"

namespace {

struct TempDatabase {
	std::filesystem::path path = std::filesystem::temp_directory_path()
		/ ("modpile-import-test-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".db");
	~TempDatabase() {
		std::error_code ignored;
		std::filesystem::remove(path, ignored);
		std::filesystem::remove(path.string() + "-wal", ignored);
		std::filesystem::remove(path.string() + "-shm", ignored);
	}
};

sqlite3 *make_source(const std::filesystem::path &path) {
	REQUIRE(db_init(path).success);
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
	return db;
}

sqlite3 *make_destination() {
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
	REQUIRE(db_migrate(db));
	return db;
}

int64_t scalar(sqlite3 *db, const char *sql) {
	sqlite3_stmt *stmt = nullptr;
	REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
	REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
	const auto value = sqlite3_column_int64(stmt, 0);
	sqlite3_finalize(stmt);
	return value;
}

std::vector<std::byte> minimal_mod() {
	// One empty ProTracker pattern with 31 empty sample headers.
	std::vector<std::byte> data(1084 + 1024);
	const std::string title = "Imported module";
	for(size_t i = 0; i < title.size(); ++i) data[i] = static_cast<std::byte>(title[i]);
	data[950] = std::byte{1};
	data[1080] = std::byte{'M'};
	data[1081] = std::byte{'.'};
	data[1082] = std::byte{'K'};
	data[1083] = std::byte{'.'};
	return data;
}

std::string insert_source_file(sqlite3 *db, std::string_view name,
		const std::vector<std::byte> &raw) {
	const auto id = calc_sha1(std::span<const std::byte>(raw));
	std::vector<std::byte> compressed(ZSTD_compressBound(raw.size()));
	const size_t compressed_size = ZSTD_compress(
		compressed.data(), compressed.size(), raw.data(), raw.size(), 3);
	REQUIRE_FALSE(ZSTD_isError(compressed_size));
	compressed.resize(compressed_size);
	sqlite3_stmt *stmt = nullptr;
	REQUIRE(sqlite3_prepare_v2(db,
		"INSERT INTO file(id,name,size,data) VALUES(?1,?2,?3,?4)", -1, &stmt, nullptr) == SQLITE_OK);
	sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, name.data(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(raw.size()));
	sqlite3_bind_blob(stmt, 4, compressed.data(), static_cast<int>(compressed.size()), SQLITE_TRANSIENT);
	REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
	sqlite3_finalize(stmt);
	return id;
}

} // namespace

TEST_CASE("database import validation rejects files without the ModPile stamp", "[database-import]") {
	TempDatabase file;
	sqlite3 *db = nullptr;
	REQUIRE(sqlite3_open(file.path.c_str(), &db) == SQLITE_OK);
	REQUIRE(db_migrate(db));
	REQUIRE(db_set_database_epoch(db, MODPILE_DATABASE_EPOCH));
	sqlite3_close(db);

	const auto inspection = inspect_database_import(file.path);
	CHECK_FALSE(inspection.compatible);
	CHECK(inspection.error_message.find("stamped") != std::string::npos);
}

TEST_CASE("database import validation requires the current migration version", "[database-import]") {
	TempDatabase file;
	sqlite3 *db = make_source(file.path);
	REQUIRE(sqlite3_exec(db, "DELETE FROM schema_migration WHERE version=3", nullptr, nullptr, nullptr) == SQLITE_OK);
	sqlite3_close(db);

	const auto inspection = inspect_database_import(file.path);
	CHECK_FALSE(inspection.compatible);
	CHECK(inspection.migration_version == 2);
	CHECK(inspection.error_message.find("V2") != std::string::npos);
}

TEST_CASE("database import additively imports selected data", "[database-import]") {
	TempDatabase file;
	sqlite3 *source = make_source(file.path);
	const auto raw = minimal_mod();
	const auto imported_id = insert_source_file(source, "new.mod", raw);
	const auto source_sql = std::format(R"(
		INSERT INTO meta(id,md5,todo,file_name,file_size,name,type,bpm,duration,loudness,audible_duration)
		VALUES('{}','untrusted-md5',0,'wrong.mod',1,'Untrusted source metadata','wrong',1,1,-5.0,1);
		INSERT INTO play(id,rating_total,rating_count,trash,played,skipped,duration)
		VALUES('shared-id',10,2,1,3,4,500);
		INSERT INTO playlist(name,description) VALUES('Source list','Imported');
		INSERT INTO playlist_track(playlist_id,track_id,track_order)
		VALUES(last_insert_rowid(),'{}',7);
	)", imported_id, imported_id);
	REQUIRE(sqlite3_exec(source, source_sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
	sqlite3_close(source);

	const auto inspection = inspect_database_import(file.path);
	REQUIRE(inspection.compatible);
	CHECK(inspection.migration_version == *db_epoch_final_migration_version(inspection.epoch));
	CHECK(inspection.epoch == MODPILE_DATABASE_EPOCH);

	sqlite3 *destination = make_destination();
	REQUIRE(sqlite3_exec(destination, R"(
		INSERT INTO play(id,rating_total,rating_count,trash,played,skipped,duration)
		VALUES('shared-id',5,1,2,6,8,1000);
		INSERT INTO playlist(name,description) VALUES('Existing list',NULL);
	)", nullptr, nullptr, nullptr) == SQLITE_OK);

	TaskControl tc;
	REQUIRE(import_database(tc, destination, file.path, {}));
	const auto file_count_sql = std::format("SELECT count(*) FROM file WHERE id='{}'", imported_id);
	const auto meta_count_sql = std::format("SELECT count(*) FROM meta WHERE id='{}'", imported_id);
	CHECK(scalar(destination, file_count_sql.c_str()) == 1);
	CHECK(scalar(destination, meta_count_sql.c_str()) == 1);
	CHECK(scalar(destination, "SELECT count(*) FROM meta WHERE name='Untrusted source metadata'") == 0);
	CHECK(scalar(destination, "SELECT rating_total FROM play WHERE id='shared-id'") == 15);
	CHECK(scalar(destination, "SELECT rating_count FROM play WHERE id='shared-id'") == 3);
	CHECK(scalar(destination, "SELECT played FROM play WHERE id='shared-id'") == 9);
	CHECK(scalar(destination, "SELECT count(*) FROM playlist") == 2);
	const auto track_sql = std::format(
		"SELECT count(*) FROM playlist_track WHERE track_id='{}' AND track_order=7", imported_id);
	CHECK(scalar(destination, track_sql.c_str()) == 1);
	sqlite3_close(destination);
}

TEST_CASE("epoch 0 file import validates size and SHA-1", "[database-import]") {
	TempDatabase file;
	sqlite3 *source = make_source(file.path);
	insert_source_file(source, "bad.mod", minimal_mod());
	SECTION("recorded size") {
		REQUIRE(sqlite3_exec(source, "UPDATE file SET size=size+1", nullptr, nullptr, nullptr) == SQLITE_OK);
	}
	SECTION("recorded SHA-1") {
		REQUIRE(sqlite3_exec(source,
			"UPDATE file SET id='0000000000000000000000000000000000000000'",
			nullptr, nullptr, nullptr) == SQLITE_OK);
	}
	sqlite3_close(source);

	sqlite3 *destination = make_destination();
	TaskControl tc;
	CHECK_FALSE(import_database(tc, destination, file.path, {true, false, false}));
	CHECK(tc.snapshot().outcome == TaskStatus::Outcome::Failed);
	CHECK(scalar(destination, "SELECT count(*) FROM file") == 0);
	CHECK(scalar(destination, "SELECT count(*) FROM meta") == 0);
	sqlite3_close(destination);
}

TEST_CASE("epoch 0 file import skips modules rejected by libxmp", "[database-import]") {
	TempDatabase file;
	sqlite3 *source = make_source(file.path);
	const std::vector<std::byte> unsupported(64, std::byte{0x5a});
	const auto unsupported_id = insert_source_file(source, "unsupported.mod", unsupported);
	const auto valid_id = insert_source_file(source, "valid.mod", minimal_mod());
	sqlite3_close(source);

	sqlite3 *destination = make_destination();
	TaskControl tc;
	REQUIRE(import_database(tc, destination, file.path, {true, false, false}));
	const auto unsupported_sql = std::format(
		"SELECT count(*) FROM file WHERE id='{}'", unsupported_id);
	const auto valid_sql = std::format("SELECT count(*) FROM file WHERE id='{}'", valid_id);
	CHECK(scalar(destination, unsupported_sql.c_str()) == 0);
	CHECK(scalar(destination, valid_sql.c_str()) == 1);
	CHECK(tc.snapshot().outcome == TaskStatus::Outcome::Succeeded);
	sqlite3_close(destination);
}

TEST_CASE("database import honors category selection", "[database-import]") {
	TempDatabase file;
	sqlite3 *source = make_source(file.path);
	REQUIRE(sqlite3_exec(source, R"(
		INSERT INTO play(id,rating_total,rating_count,trash,played,skipped,duration)
		VALUES('only-play',2,1,0,1,0,10);
		INSERT INTO playlist(name,description) VALUES('Not selected',NULL);
	)", nullptr, nullptr, nullptr) == SQLITE_OK);
	sqlite3_close(source);

	sqlite3 *destination = make_destination();
	DatabaseImportOptions options{false, true, false};
	TaskControl tc;
	REQUIRE(import_database(tc, destination, file.path, options));
	CHECK(scalar(destination, "SELECT count(*) FROM play WHERE id='only-play'") == 1);
	CHECK(scalar(destination, "SELECT count(*) FROM playlist") == 0);
	sqlite3_close(destination);
}

TEST_CASE("database import preserves playlist track ownership", "[database-import]") {
	TempDatabase file;
	sqlite3 *source = make_source(file.path);
	REQUIRE(sqlite3_exec(source, R"(
		INSERT INTO playlist(name, description) VALUES('First', NULL);
		INSERT INTO playlist_track(playlist_id, track_id, track_order)
		VALUES(last_insert_rowid(), 'first-track', 0);
		INSERT INTO playlist(name, description) VALUES('Second', NULL);
		INSERT INTO playlist_track(playlist_id, track_id, track_order)
		VALUES(last_insert_rowid(), 'second-track', 0);
	)", nullptr, nullptr, nullptr) == SQLITE_OK);
	sqlite3_close(source);

	sqlite3 *destination = make_destination();
	TaskControl tc;
	REQUIRE(import_database(tc, destination, file.path, {false, false, true}));
	CHECK(scalar(destination, R"(
		SELECT count(*)
		FROM playlist_track
		JOIN playlist ON playlist.id = playlist_track.playlist_id
		WHERE playlist.name = 'First' AND playlist_track.track_id = 'first-track'
	)") == 1);
	CHECK(scalar(destination, R"(
		SELECT count(*)
		FROM playlist_track
		JOIN playlist ON playlist.id = playlist_track.playlist_id
		WHERE playlist.name = 'Second' AND playlist_track.track_id = 'second-track'
	)") == 1);
	CHECK(scalar(destination, R"(
		SELECT count(*)
		FROM playlist_track
		JOIN playlist ON playlist.id = playlist_track.playlist_id
		WHERE playlist.name = 'First' AND playlist_track.track_id = 'second-track'
	)") == 0);
	sqlite3_close(destination);
}
