// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <sqlite3.h>

constexpr int32_t modpile_fourcc(char a, char b, char c, char d) {
	return (int32_t(uint8_t(a)) << 24) | (int32_t(uint8_t(b)) << 16)
	     | (int32_t(uint8_t(c)) <<  8) |  int32_t(uint8_t(d));
}

// Stored in SQLite's application_id header field.
inline constexpr int32_t MODPILE_APPLICATION_ID = modpile_fourcc('M', 'P', 'L', 'E');

struct DatabaseInitializationResult {
	bool success;
	std::string error_message;
};

sqlite3* db_open(const std::filesystem::path &path);
[[nodiscard]] DatabaseInitializationResult db_init(const std::filesystem::path &path);

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
