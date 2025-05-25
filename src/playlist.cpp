// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "playlist.h"

#include <optional>
#include <utility>
#include <sqlite3.h>

#include "db.h"
#include "log.h"
#include "util/sqlite_util.h"

static sqlite3 *g_playlistConnection = nullptr;

static std::optional<std::string> sqlite_column_optional_string(sqlite3_stmt *stmt, int col) {
	if(sqlite3_column_type(stmt, col) == SQLITE_NULL) {
		return std::nullopt;
	}
	return sqlite3_column_string(stmt, col);
}

static void load_playlists(AppState &app) {
	auto sql = R"(
		SELECT
			playlist.id,
			playlist.name,
			playlist.description,
			(
				SELECT COUNT(*)
				FROM playlist_track
				WHERE playlist_track.playlist_id = playlist.id
			) AS track_count
		FROM playlist
		ORDER BY playlist.name ASC
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_playlistConnection, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
	
	std::vector<AppState::Playlist::State::PlaylistRow> rows;
	while(true) {
		auto s = sqlite3_step(stmt);
		if(s == SQLITE_DONE) {
			break;
		}
		if(s != SQLITE_ROW) {
			log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
			break;
		}
		
		AppState::Playlist::State::PlaylistRow row;
		row.id = sqlite3_column_int64(stmt, 0);
		row.name = sqlite3_column_string(stmt, 1);
		row.description = sqlite_column_optional_string(stmt, 2);
		row.track_count = sqlite3_column_int64(stmt, 3);
		rows.emplace_back(std::move(row));
	}
	
	app.playlist.state.playlists = std::move(rows);
}

static void load_playlist_tracks(AppState &app, int64_t playlist_id) {
	auto sql = R"(
		SELECT
			playlist_track.id,
			playlist_track.playlist_id,
			playlist_track.track_order,
			meta.id,
			meta.file_name,
			meta.file_size,
			modland.artist,
			meta.name,
			meta.bpm,
			meta.duration,
			meta.loudness,
			play.rating
		FROM
			playlist_track
		JOIN
			meta ON meta.id == playlist_track.track_id
		LEFT JOIN
			play ON play.id == meta.id
		LEFT JOIN
			modland ON modland.md5 == meta.md5
		WHERE
			playlist_track.playlist_id == ?1
		ORDER BY
			playlist_track.track_order ASC, playlist_track.id ASC
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_playlistConnection, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
	
	sqlite3_bind_int64(stmt, 1, playlist_id);
	
	std::vector<AppState::Playlist::State::Row> rows;
	while(true) {
		auto s = sqlite3_step(stmt);
		if(s == SQLITE_DONE) {
			break;
		}
		if(s != SQLITE_ROW) {
			log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
			break;
		}
		
		AppState::Playlist::State::Row row;
		row.playlist_track_id = sqlite3_column_int64(stmt, 0);
		row.playlist_id = sqlite3_column_int64(stmt, 1);
		row.track_order = sqlite3_column_int64(stmt, 2);
		row.id = sqlite3_column_string(stmt, 3);
		row.file_name = sqlite3_column_string(stmt, 4);
		row.file_size = sqlite3_column_int64(stmt, 5);
		row.artist = sqlite3_column_string(stmt, 6);
		row.name = sqlite3_column_string(stmt, 7);
		row.bpm = sqlite_util_column_int64(stmt, 8);
		row.duration = sqlite_util_column_int64(stmt, 9);
		row.loudness = sqlite_util_column_double(stmt, 10);
		row.rating = sqlite_util_column_double(stmt, 11);
		
		rows.emplace_back(std::move(row));
	}
	
	app.playlist.state.rows = std::move(rows);
}

static void create_playlist(const AppState::Playlist::Request::CreatePlaylist &req) {
	auto sql = R"(
		INSERT INTO playlist(name, description)
		VALUES(?1, ?2)
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_playlistConnection, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
	
	sqliteu_bind_string(stmt, 1, req.name);
	sqliteu_bind_optional_string(stmt, 2, req.description);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
	
}

static void update_playlist(const AppState::Playlist::Request::UpdatePlaylist &req) {
	std::string sql;
	bool setDescription = req.description.has_value() || req.clearDescription;
	if(setDescription) {
		sql = R"(
			UPDATE playlist
			SET name = ?2, description = ?3
			WHERE id = ?1
			;
		)";
	} else {
		sql = R"(
			UPDATE playlist
			SET name = ?2
			WHERE id = ?1
			;
		)";
	}
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_playlistConnection, sql.c_str(), -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
	
	sqlite3_bind_int64(stmt, 1, req.id);
	sqliteu_bind_string(stmt, 2, req.name);
	if(setDescription) {
		sqliteu_bind_optional_string(stmt, 3, req.description);
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
}

static void delete_playlist(const AppState::Playlist::Request::DeletePlaylist &req) {
	auto sql = R"(
		DELETE FROM playlist
		WHERE id = ?1
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_playlistConnection, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
	
	sqlite3_bind_int64(stmt, 1, req.id);
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
}

static std::optional<int64_t> next_track_order(int64_t playlist_id) {
	auto sql = R"(
		SELECT IFNULL(MAX(track_order), 0)
		FROM playlist_track
		WHERE playlist_id == ?1
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_playlistConnection, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		return std::nullopt;
	}
	
	sqlite3_bind_int64(stmt, 1, playlist_id);
	
	auto s = sqlite3_step(stmt);
	if(s == SQLITE_ROW) {
		return sqlite3_column_int64(stmt, 0) + 1;
	}
	if(s != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
	}
	return std::nullopt;
}

static void add_track(const AppState::Playlist::Request::AddTrack &req) {
	if(!sqliteu_begin(g_playlistConnection)) {
		log_error("BEGIN failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}

	auto order = next_track_order(req.playlist_id);
	if(!order.has_value()) {
		sqliteu_rollback(g_playlistConnection);
		return;
	}

	auto sql = R"(
		INSERT INTO playlist_track(playlist_id, track_id, track_order)
		VALUES(?1, ?2, ?3)
		;
	)";

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_playlistConnection, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		sqliteu_rollback(g_playlistConnection);
		return;
	}

	sqlite3_bind_int64(stmt, 1, req.playlist_id);
	sqliteu_bind_string(stmt, 2, req.track_id);
	sqlite3_bind_int64(stmt, 3, order.value());

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
		sqliteu_rollback(g_playlistConnection);
		return;
	}

	if(!sqliteu_commit(g_playlistConnection)) {
		log_error("COMMIT failed: {}", sqlite3_errmsg(g_playlistConnection));
	}
}

static void remove_track(const AppState::Playlist::Request::RemoveTrack &req) {
	if(!sqliteu_begin(g_playlistConnection)) {
		log_error("BEGIN failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}

	auto sql = R"(
		DELETE FROM playlist_track
		WHERE id == ?1
		;
	)";

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_playlistConnection, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		sqliteu_rollback(g_playlistConnection);
		return;
	}

	sqlite3_bind_int64(stmt, 1, req.playlist_track_id);
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
		sqliteu_rollback(g_playlistConnection);
		return;
	}

	auto shiftSql = R"(
		UPDATE playlist_track
		SET track_order = track_order - 1
		WHERE playlist_id == ?1 AND track_order > ?2
		;
	)";
	SQLITE_FINALIZE sqlite3_stmt *shiftStmt = nullptr;
	rc = sqlite3_prepare_v2(g_playlistConnection, shiftSql, -1, &shiftStmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		sqliteu_rollback(g_playlistConnection);
		return;
	}
	sqlite3_bind_int64(shiftStmt, 1, req.playlist_id);
	sqlite3_bind_int64(shiftStmt, 2, req.track_order);
	rc = sqlite3_step(shiftStmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
		sqliteu_rollback(g_playlistConnection);
		return;
	}

	if(!sqliteu_commit(g_playlistConnection)) {
		log_error("COMMIT failed: {}", sqlite3_errmsg(g_playlistConnection));
	}
}

static void clear_playlist(const AppState::Playlist::Request::ClearPlaylist &req) {
	auto sql = R"(
		DELETE FROM playlist_track
		WHERE playlist_id == ?1
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_playlistConnection, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
	
	sqlite3_bind_int64(stmt, 1, req.playlist_id);
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
}

static void move_track(const AppState::Playlist::Request::MoveTrack &req) {
	if(req.from_order == req.to_order) {
		return;
	}
	
	if(!sqliteu_begin(g_playlistConnection)) {
		log_error("BEGIN failed: {}", sqlite3_errmsg(g_playlistConnection));
		return;
	}
	bool ok = true;
	
	if(req.to_order < req.from_order) {
		auto shiftSql = R"(
			UPDATE playlist_track
			SET track_order = track_order + 1
			WHERE playlist_id == ?1
				AND track_order >= ?2
				AND track_order < ?3
			;
		)";
		SQLITE_FINALIZE sqlite3_stmt *shiftStmt = nullptr;
		int rc = sqlite3_prepare_v2(g_playlistConnection, shiftSql, -1, &shiftStmt, nullptr);
		if (rc != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
			ok = false;
		}
		if(ok) {
			sqlite3_bind_int64(shiftStmt, 1, req.playlist_id);
			sqlite3_bind_int64(shiftStmt, 2, req.to_order);
			sqlite3_bind_int64(shiftStmt, 3, req.from_order);
			rc = sqlite3_step(shiftStmt);
			if (rc != SQLITE_DONE) {
				log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
				ok = false;
			}
		}
	} else {
		auto shiftSql = R"(
			UPDATE playlist_track
			SET track_order = track_order - 1
			WHERE playlist_id == ?1
				AND track_order <= ?2
				AND track_order > ?3
			;
		)";
		SQLITE_FINALIZE sqlite3_stmt *shiftStmt = nullptr;
		int rc = sqlite3_prepare_v2(g_playlistConnection, shiftSql, -1, &shiftStmt, nullptr);
		if (rc != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
			ok = false;
		}
		if(ok) {
			sqlite3_bind_int64(shiftStmt, 1, req.playlist_id);
			sqlite3_bind_int64(shiftStmt, 2, req.to_order);
			sqlite3_bind_int64(shiftStmt, 3, req.from_order);
			rc = sqlite3_step(shiftStmt);
			if (rc != SQLITE_DONE) {
				log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
				ok = false;
			}
		}
	}

	if(ok) {
		auto moveSql = R"(
			UPDATE playlist_track
			SET track_order = ?2
			WHERE id == ?1
			;
		)";
		SQLITE_FINALIZE sqlite3_stmt *moveStmt = nullptr;
		int rc = sqlite3_prepare_v2(g_playlistConnection, moveSql, -1, &moveStmt, nullptr);
		if (rc != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(g_playlistConnection));
			ok = false;
		}
		if(ok) {
			sqlite3_bind_int64(moveStmt, 1, req.playlist_track_id);
			sqlite3_bind_int64(moveStmt, 2, req.to_order);
			rc = sqlite3_step(moveStmt);
			if (rc != SQLITE_DONE) {
				log_error("step failed: {}", sqlite3_errmsg(g_playlistConnection));
				ok = false;
			}
		}
	}
	
	if(ok) {
		if(!sqliteu_commit(g_playlistConnection)) {
			log_error("COMMIT failed: {}", sqlite3_errmsg(g_playlistConnection));
		}
	} else {
		sqliteu_rollback(g_playlistConnection);
	}
}

void playlist_init(AppState &app) {
	g_playlistConnection = db_open(app);
	if(g_playlistConnection) {
		load_playlists(app);
	}
}

void playlist_iterate(AppState &app) {
	if(!g_playlistConnection) {
		return;
	}
	
	bool reloadPlaylists = false;
	bool reloadTracks = false;
	
	if(app.playlist.request.createPlaylist) {
		create_playlist(app.playlist.request.createPlaylist.value());
		app.playlist.request.createPlaylist.reset();
		reloadPlaylists = true;
	}
	if(app.playlist.request.updatePlaylist) {
		update_playlist(app.playlist.request.updatePlaylist.value());
		app.playlist.request.updatePlaylist.reset();
		reloadPlaylists = true;
	}
	if(app.playlist.request.deletePlaylist) {
		auto id = app.playlist.request.deletePlaylist->id;
		delete_playlist(app.playlist.request.deletePlaylist.value());
		app.playlist.request.deletePlaylist.reset();
		reloadPlaylists = true;
		if(app.playlist.state.current_playlist_id == id) {
			app.playlist.state.current_playlist_id.reset();
			app.playlist.state.rows.clear();
		}
	}
	if(app.playlist.request.clearPlaylist) {
		auto id = app.playlist.request.clearPlaylist->playlist_id;
		clear_playlist(app.playlist.request.clearPlaylist.value());
		app.playlist.request.clearPlaylist.reset();
		if(app.playlist.state.current_playlist_id == id) {
			reloadTracks = true;
		}
	}
	if(app.playlist.request.addTrack) {
		auto id = app.playlist.request.addTrack->playlist_id;
		add_track(app.playlist.request.addTrack.value());
		app.playlist.request.addTrack.reset();
		if(app.playlist.state.current_playlist_id == id) {
			reloadTracks = true;
		}
	}
	if(app.playlist.request.removeTrack) {
		remove_track(app.playlist.request.removeTrack.value());
		app.playlist.request.removeTrack.reset();
		reloadTracks = true;
	}
	if(app.playlist.request.moveTrack) {
		move_track(app.playlist.request.moveTrack.value());
		app.playlist.request.moveTrack.reset();
		reloadTracks = true;
	}
	if(app.playlist.request.loadPlaylist.has_value()) {
		app.playlist.state.current_playlist_id = app.playlist.request.loadPlaylist.value();
		app.playlist.request.loadPlaylist.reset();
		reloadTracks = true;
	}
	if(app.playlist.request.reloadPlaylists) {
		app.playlist.request.reloadPlaylists = false;
		reloadPlaylists = true;
	}
	
	if(reloadPlaylists) {
		load_playlists(app);
	}
	if(reloadTracks) {
		if(app.playlist.state.current_playlist_id.has_value()) {
			load_playlist_tracks(app, app.playlist.state.current_playlist_id.value());
		} else {
			app.playlist.state.rows.clear();
		}
	}
}

void playlist_quit() {
	if(g_playlistConnection) {
		sqlite3_close(g_playlistConnection);
		g_playlistConnection = nullptr;
	}
}
