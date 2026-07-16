#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/benchmark/catch_chronometer.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "../src/db/table/table_import_export.h"

namespace {

sqlite3 *create_benchmark_db() {
	sqlite3 *db = nullptr;
	if(sqlite3_open(":memory:", &db) != SQLITE_OK) return nullptr;
	if(sqlite3_exec(db, R"(
		CREATE TABLE sample (
			id      INTEGER PRIMARY KEY,
			name    TEXT NOT NULL,
			score   REAL NOT NULL,
			payload BLOB
		) STRICT;
	)", nullptr, nullptr, nullptr) != SQLITE_OK) {
		sqlite3_close(db);
		return nullptr;
	}
	return db;
}

std::string make_text_import(size_t rows) {
	std::string input = "id\tname\tscore\tpayload\n";
	input.reserve(input.size() + rows * 48);
	for(size_t i = 0; i < rows; ++i) {
		input += std::to_string(i);
		input += "\ttrack\\tname-";
		input += std::to_string(i);
		input += "\t123.5\t\\N\n";
	}
	return input;
}

std::string make_blob_import(size_t rows, size_t encodedBytes) {
	std::string input = "id\tname\tscore\tpayload\n";
	const std::string payload(encodedBytes, 'A');
	input.reserve(input.size() + rows * (encodedBytes + 32));
	for(size_t i = 0; i < rows; ++i) {
		input += std::to_string(i);
		input += "\ttrack\t1.0\t\\B";
		input += payload;
		input += '\n';
	}
	return input;
}

bool populate_for_export(sqlite3 *db, size_t rows, size_t blobBytes) {
	if(sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
	sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db,
		"INSERT INTO sample(id, name, score, payload) VALUES(?1, ?2, ?3, ?4)",
		-1, &stmt, nullptr) != SQLITE_OK) return false;
	const std::string payload(blobBytes, 'x');
	for(size_t i = 0; i < rows; ++i) {
		sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(i));
		sqlite3_bind_text(stmt, 2, "track name", -1, SQLITE_STATIC);
		sqlite3_bind_double(stmt, 3, 123.5);
		if(blobBytes == 0) {
			sqlite3_bind_null(stmt, 4);
		} else {
			sqlite3_bind_blob64(stmt, 4, payload.data(), payload.size(), SQLITE_STATIC);
		}
		if(sqlite3_step(stmt) != SQLITE_DONE) {
			sqlite3_finalize(stmt);
			return false;
		}
		sqlite3_reset(stmt);
		sqlite3_clear_bindings(stmt);
	}
	sqlite3_finalize(stmt);
	return sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
}

void benchmark_import(Catch::Benchmark::Chronometer meter, std::string_view input) {
	// Catch2 may invoke the measured callable repeatedly. Each iteration needs a
	// fresh table, so database creation is included in the end-to-end import
	// measurement. The fixed cost is exposed by the 1k/10k scaling comparison.
	meter.measure([&] {
		sqlite3 *db = create_benchmark_db();
		if(!db) return false;
		std::istringstream stream{std::string(input)};
		const bool imported = db_import_table(db, "sample", stream);
		sqlite3_close(db);
		return imported;
	});
}

void benchmark_export(Catch::Benchmark::Chronometer meter, size_t rows, size_t blobBytes) {
	sqlite3 *db = create_benchmark_db();
	REQUIRE(db != nullptr);
	REQUIRE(populate_for_export(db, rows, blobBytes));
	bool exported = false;
	meter.measure([&] {
		std::ostringstream stream;
		exported = db_export_table(db, "sample", stream);
	});
	REQUIRE(exported);
	sqlite3_close(db);
}

void benchmark_raw_sqlite_insert(Catch::Benchmark::Chronometer meter, size_t rows) {
	meter.measure([&] {
		sqlite3 *db = create_benchmark_db();
		if(!db || sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
		sqlite3_stmt *stmt = nullptr;
		if(sqlite3_prepare_v2(db,
			"INSERT INTO sample(id, name, score, payload) VALUES(?1, ?2, ?3, NULL)",
			-1, &stmt, nullptr) != SQLITE_OK) {
			sqlite3_close(db);
			return false;
		}
		bool ok = true;
		for(size_t i = 0; i < rows; ++i) {
			sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(i));
			sqlite3_bind_text(stmt, 2, "track\tname", -1, SQLITE_STATIC);
			sqlite3_bind_double(stmt, 3, 123.5);
			if(sqlite3_step(stmt) != SQLITE_DONE) {
				ok = false;
				break;
			}
			sqlite3_reset(stmt);
		}
		sqlite3_finalize(stmt);
		ok = ok && sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
		sqlite3_close(db);
		return ok;
	});
}

} // namespace

TEST_CASE("generic table import export performance", "[!benchmark][db][table]") {
	const auto text1k = make_text_import(1'000);
	const auto text10k = make_text_import(10'000);
	const auto text100k = make_text_import(100'000);
	const auto blob1k = make_blob_import(1'000, 1'024);

	BENCHMARK_ADVANCED("import 1,000 text rows")(Catch::Benchmark::Chronometer meter) {
		benchmark_import(meter, text1k);
	};
	BENCHMARK_ADVANCED("import 10,000 text rows")(Catch::Benchmark::Chronometer meter) {
		benchmark_import(meter, text10k);
	};
	BENCHMARK_ADVANCED("import 100,000 text rows")(Catch::Benchmark::Chronometer meter) {
		benchmark_import(meter, text100k);
	};
	BENCHMARK_ADVANCED("raw SQLite insert 100,000 rows")(Catch::Benchmark::Chronometer meter) {
		benchmark_raw_sqlite_insert(meter, 100'000);
	};
	BENCHMARK_ADVANCED("import 1,000 768-byte blobs")(Catch::Benchmark::Chronometer meter) {
		benchmark_import(meter, blob1k);
	};
	BENCHMARK_ADVANCED("export 1,000 text rows")(Catch::Benchmark::Chronometer meter) {
		benchmark_export(meter, 1'000, 0);
	};
	BENCHMARK_ADVANCED("export 10,000 text rows")(Catch::Benchmark::Chronometer meter) {
		benchmark_export(meter, 10'000, 0);
	};
	BENCHMARK_ADVANCED("export 100,000 text rows")(Catch::Benchmark::Chronometer meter) {
		benchmark_export(meter, 100'000, 0);
	};
	BENCHMARK_ADVANCED("export 1,000 1-KiB blobs")(Catch::Benchmark::Chronometer meter) {
		benchmark_export(meter, 1'000, 1'024);
	};
}
