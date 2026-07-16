// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "db.h"


#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>

#include <sqlite3.h>

#include "db/schema/schema.h"
#include "global.h"
#include "util/sqlite_util.h"
#include "log.h"


static bool exec(sqlite3* db, const char* sql) {
	SQLITE_FREE char *msg = nullptr;
	int rc = sqlite3_exec(db, sql, 0, 0, &msg);
	if(rc != SQLITE_OK){
		log_error("SQL error: {} -> {}", sql, msg);
		return false;
	}
	return true;
}

static void enable_pragmas(sqlite3* db) {
	// Retry for up to 10 s if another connection is finishing its checkpoint.
	sqlite3_busy_timeout(db, 10'000);

	auto sql = R"(
		PRAGMA journal_mode = WAL;
		PRAGMA synchronous = NORMAL;
		PRAGMA foreign_keys = ON;
	)";
	exec(db, sql);
}

sqlite3* db_open(const AppState & app) {
	
	int r;
	
	auto dbPath = app.config.database.path;
	
	int flags = 0;
	flags |= SQLITE_OPEN_EXRESCODE;
	flags |= SQLITE_OPEN_READWRITE;
	flags |= SQLITE_OPEN_CREATE;
	flags |= SQLITE_OPEN_NOMUTEX;
	flags |= SQLITE_OPEN_NOFOLLOW;
	
	sqlite3* db;
	r = sqlite3_open_v2(dbPath.c_str(), &db, flags, nullptr);
	if (r != SQLITE_OK) {
		log_error("Error open DB: {} ({})", sqlite3_errmsg(db), r);
		sqlite3_close(db);
		return nullptr;
	}
	
	enable_pragmas(db);
	
	return db;
}

constexpr int32_t fourcc(char a, char b, char c, char d) {
	return (int32_t(uint8_t(a)) << 24) | (int32_t(uint8_t(b)) << 16)
	     | (int32_t(uint8_t(c)) <<  8) |  int32_t(uint8_t(d));
}

// Stored in the SQLite file header (bytes 60-63). Identifies this as a ModPile database.
// See: https://www.sqlite.org/pragma.html#pragma_application_id
static constexpr int32_t MODPILE_APPLICATION_ID = fourcc('M','P','L','E');

static std::optional<bool> db_is_empty(sqlite3 *db) {
	auto sql = R"(
		SELECT COUNT(*)
		FROM sqlite_schema
		WHERE name NOT LIKE 'sqlite_%'
		;
	)";

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		log_error("Failed to inspect database schema: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}

	if(sqlite3_step(stmt) != SQLITE_ROW) {
		log_error("Failed to inspect database schema: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}

	return sqlite3_column_int64(stmt, 0) == 0;
}

bool db_init(AppState &app) {

	SQLITE_CLOSE sqlite3* db = db_open(app);
	if(!db) {
		return false;
	}

	// Verify (or stamp) the application_id so we don't accidentally pollute a foreign database.
	{
		SQLITE_FINALIZE sqlite3_stmt* stmt = nullptr;
		int32_t app_id = 0;
		if(sqlite3_prepare_v2(db, "PRAGMA application_id", -1, &stmt, nullptr) != SQLITE_OK) {
			log_error("Failed to read database application_id: {}", sqlite3_errmsg(db));
			app.setup.error_message = "Could not inspect the selected database file.";
			return false;
		}
		if(sqlite3_step(stmt) == SQLITE_ROW) {
			app_id = sqlite3_column_int(stmt, 0);
		} else {
			log_error("Failed to read database application_id: {}", sqlite3_errmsg(db));
			app.setup.error_message = "Could not inspect the selected database file.";
			return false;
		}

		if(app_id == 0) {
			auto empty = db_is_empty(db);
			if(!empty.has_value()) {
				app.setup.error_message = "Could not inspect the selected database file.";
				return false;
			}
			if(!empty.value()) {
				log_error("Refusing to stamp database with unset application_id because it is not empty");
				app.setup.error_message = "That database is not empty and is not marked as a ModPile database.";
				return false;
			}

			// Fresh empty database — stamp it as ours.
			auto sql = std::format("PRAGMA application_id = {}", MODPILE_APPLICATION_ID);
			if(!exec(db, sql.c_str())) {
				app.setup.error_message = "Could not mark the selected database file as a ModPile database.";
				return false;
			}
		} else if(app_id != MODPILE_APPLICATION_ID) {
			log_error("Refusing to open database: application_id 0x{:08X} does not match ModPile (0x{:08X})",
				static_cast<uint32_t>(app_id), static_cast<uint32_t>(MODPILE_APPLICATION_ID));
			app.setup.error_message = "That file belongs to a different application and cannot be opened as a ModPile database.";
			return false;
		}
	}

	if(!db_migrate(db)) {
		return false;
	}

	return true;
}

void updatePlayback(sqlite3* db, const PlayData &pd) {
	
	log_debug("Updating playback stats for: {}", pd.id.c_str());
	
	auto sql = R"(
		INSERT INTO play(id, rating_total, rating_count, trash, played, skipped, duration)
			VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)
			ON CONFLICT(id) DO UPDATE SET
				rating_total = rating_total + ?2,
				rating_count = rating_count + ?3,
				trash = trash + ?4,
				played = played + ?5,
				skipped = skipped + ?6,
				duration = duration + ?7;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = NULL;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return;
	}
	
	int64_t rating_total = 0;
	int64_t rating_count = 0;
	if(pd.rating.has_value()) {
		rating_total = pd.rating.value();
		rating_count = 1;
	}
	
	sqlite3_bind_text (stmt, 1, pd.id.c_str(), pd.id.length(), SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, rating_total);
	sqlite3_bind_int64(stmt, 3, rating_count);
	sqlite3_bind_int64(stmt, 4, pd.trash);
	sqlite3_bind_int64(stmt, 5, pd.played);
	sqlite3_bind_int64(stmt, 6, pd.skipped);
	sqlite3_bind_int64(stmt, 7, pd.duration);
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
	}
	
	// sqlite3_db_cacheflush(db);
}

std::optional<double> db_get_rating(sqlite3* db, const std::string id) {

	auto sql = R"(
		SELECT rating
		FROM play
		WHERE id == ?1
		  AND rating_count > 0
		LIMIT 1;
	)";

	SQLITE_FINALIZE sqlite3_stmt *stmt = NULL;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}

	sqlite3_bind_text(stmt, 1, id.data(), id.length(), SQLITE_STATIC);

	auto s = sqlite3_step(stmt);
	if(s == SQLITE_DONE) {
		return std::nullopt;
	}
	if(s != SQLITE_ROW) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}

	return sqlite_util_column_double(stmt, 0);
}

std::optional<std::string> db_get_random(sqlite3* db) {

	// Naive solution is too slow:
	// "SELECT ID, DATA FROM file ORDER BY RANDOM() LIMIT 1;"
	//
	// The UNION ALL branch handles gaps at the tail end of the table (deleted
	// rows): if no row exists with rowid >= the random threshold, fall back to
	// the first row in the table (wrap-around). COALESCE handles empty tables.
	// SQLite random() deliberately avoids INT64_MIN, so ABS(RANDOM()) cannot
	// overflow here.
	auto sql = R"(
		SELECT file.id FROM file
		WHERE rowid >= ABS(RANDOM()) % (SELECT COALESCE(max(rowid), 0) + 1 FROM file)
		UNION ALL
		SELECT file.id FROM file
		LIMIT 1
		;
	)";

	SQLITE_FINALIZE sqlite3_stmt *stmt = NULL;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}

	auto t0 = std::chrono::steady_clock::now();
	auto s = sqlite3_step(stmt);
	auto t1 = std::chrono::steady_clock::now();
	log_debug("db_get_random: {}us", std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

	if(s == SQLITE_DONE) {
		return std::nullopt;
	}
	if(s != SQLITE_ROW) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}

	return sqlite3_column_string(stmt, 0);
}

static std::optional<PlaylistTrackRef> db_get_playlist_track_query(sqlite3* db, const char* sql, int64_t param) {
	SQLITE_FINALIZE sqlite3_stmt *stmt = NULL;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}
	sqlite3_bind_int64(stmt, 1, param);
	auto s = sqlite3_step(stmt);
	if(s != SQLITE_ROW) {
		return std::nullopt;
	}
	PlaylistTrackRef ref;
	ref.playlist_track_id = sqlite3_column_int64(stmt, 0);
	ref.track_id = sqlite3_column_string(stmt, 1);
	return ref;
}

std::optional<PlaylistTrackRef> db_get_next_playlist_track(sqlite3* db, int64_t current_playlist_track_id) {
	auto sql = R"(
		SELECT pt2.id, pt2.track_id
		FROM playlist_track pt1
		JOIN playlist_track pt2
		  ON pt2.playlist_id = pt1.playlist_id
		 AND pt2.track_order > pt1.track_order
		WHERE pt1.id = ?1
		ORDER BY pt2.track_order ASC
		LIMIT 1
		;
	)";
	return db_get_playlist_track_query(db, sql, current_playlist_track_id);
}

std::optional<PlaylistTrackRef> db_get_prev_playlist_track(sqlite3* db, int64_t current_playlist_track_id) {
	auto sql = R"(
		SELECT pt2.id, pt2.track_id
		FROM playlist_track pt1
		JOIN playlist_track pt2
		  ON pt2.playlist_id = pt1.playlist_id
		 AND pt2.track_order < pt1.track_order
		WHERE pt1.id = ?1
		ORDER BY pt2.track_order DESC
		LIMIT 1
		;
	)";
	return db_get_playlist_track_query(db, sql, current_playlist_track_id);
}

std::optional<PlaylistTrackRef> db_get_first_playlist_track(sqlite3* db, int64_t current_playlist_track_id) {
	auto sql = R"(
		SELECT pt2.id, pt2.track_id
		FROM playlist_track pt1
		JOIN playlist_track pt2 ON pt2.playlist_id = pt1.playlist_id
		WHERE pt1.id = ?1
		ORDER BY pt2.track_order ASC, pt2.id ASC
		LIMIT 1
		;
	)";
	return db_get_playlist_track_query(db, sql, current_playlist_track_id);
}

std::optional<PlaylistTrackRef> db_get_last_playlist_track(sqlite3* db, int64_t current_playlist_track_id) {
	auto sql = R"(
		SELECT pt2.id, pt2.track_id
		FROM playlist_track pt1
		JOIN playlist_track pt2 ON pt2.playlist_id = pt1.playlist_id
		WHERE pt1.id = ?1
		ORDER BY pt2.track_order DESC, pt2.id DESC
		LIMIT 1
		;
	)";
	return db_get_playlist_track_query(db, sql, current_playlist_track_id);
}

std::optional<PlaylistTrackRef> db_get_random_playlist_track(sqlite3* db, int64_t current_playlist_track_id) {
	// Exclude the currently-playing track to avoid immediate repeats.
	// Falls back to including it when the playlist has only one track.
	auto sql = R"(
		SELECT pt2.id, pt2.track_id
		FROM playlist_track pt1
		JOIN playlist_track pt2 ON pt2.playlist_id = pt1.playlist_id
		WHERE pt1.id = ?1 AND pt2.id != pt1.id
		ORDER BY RANDOM()
		LIMIT 1
		;
	)";
	auto result = db_get_playlist_track_query(db, sql, current_playlist_track_id);
	if(result) return result;

	// Single-track playlist: pick it anyway.
	auto fallback = R"(
		SELECT pt2.id, pt2.track_id
		FROM playlist_track pt1
		JOIN playlist_track pt2 ON pt2.playlist_id = pt1.playlist_id
		WHERE pt1.id = ?1
		ORDER BY RANDOM()
		LIMIT 1
		;
	)";
	return db_get_playlist_track_query(db, fallback, current_playlist_track_id);
}

std::optional<double> db_get_loudness(sqlite3* db, const std::string id) {
	
	auto sql = R"(
		SELECT loudness
		FROM meta
		WHERE id == ?1
		LIMIT 1;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = NULL;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}
	
	sqlite3_bind_text(stmt, 1, id.data(), id.length(), SQLITE_STATIC);
	
	auto s = sqlite3_step(stmt);
	if(s == SQLITE_DONE) {
		return std::nullopt;
	}
	if(s != SQLITE_ROW) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}

	return sqlite_util_column_double(stmt, 0);
}

std::optional<int64_t> db_get_audible_duration(sqlite3* db, const std::string id) {
	auto sql = R"(
		SELECT audible_duration
		FROM meta
		WHERE id == ?1
		LIMIT 1;
	)";

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}

	sqlite3_bind_text(stmt, 1, id.data(), id.length(), SQLITE_STATIC);
	const auto step = sqlite3_step(stmt);
	if(step == SQLITE_DONE) return std::nullopt;
	if(step != SQLITE_ROW) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return std::nullopt;
	}
	return sqlite_util_column_int64(stmt, 0);
}
