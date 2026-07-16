// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <sqlite3.h>

struct AppState;

sqlite3* db_open(const AppState &app);
bool db_init(AppState &app);

struct PlayData {
	std::string id = "";
	std::optional<long> rating;
	long trash = 0;
	long played = 0;
	long skipped = 0;
	long duration = 0;
};

void updatePlayback(sqlite3* db, const PlayData &pd);

std::optional<std::string> db_get_random(sqlite3 *db);

std::optional<double> db_get_rating(sqlite3* db, const std::string id);
std::optional<double> db_get_loudness(sqlite3* db, const std::string id);
std::optional<int64_t> db_get_audible_duration(sqlite3* db, const std::string id);

struct PlaylistTrackRef {
	int64_t playlist_track_id = 0;
	std::string track_id;
};

std::optional<PlaylistTrackRef> db_get_next_playlist_track(sqlite3* db, int64_t current_playlist_track_id);
std::optional<PlaylistTrackRef> db_get_prev_playlist_track(sqlite3* db, int64_t current_playlist_track_id);
std::optional<PlaylistTrackRef> db_get_first_playlist_track(sqlite3* db, int64_t current_playlist_track_id);
std::optional<PlaylistTrackRef> db_get_last_playlist_track(sqlite3* db, int64_t current_playlist_track_id);
std::optional<PlaylistTrackRef> db_get_random_playlist_track(sqlite3* db, int64_t current_playlist_track_id);
