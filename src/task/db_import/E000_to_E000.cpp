// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "db_import_internal.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <zstd.h>

#include "../../db_common.h"
#include "../../log.h"
#include "../../util/sqlite_util.h"

namespace {

struct Epoch000File {
	std::string id;
	std::string name;
	int64_t size = 0;
	std::vector<std::byte> compressed_data;
};

struct Epoch000PlayStats {
	std::string id;
	int64_t rating_total = 0;
	int64_t rating_count = 0;
	int64_t trash = 0;
	int64_t played = 0;
	int64_t skipped = 0;
	int64_t duration = 0;
};

struct Epoch000PlaylistTrack {
	std::string track_id;
	int64_t track_order = 0;
};

struct Epoch000Playlist {
	int64_t id = 0;
	std::string name;
	std::optional<std::string> description;
	std::vector<Epoch000PlaylistTrack> tracks;
};

enum class DecodeFileResult {
	success,
	invalid,
	metadata_parse_failed,
};

namespace FileColumn {
constexpr int id = 0;
constexpr int name = 1;
constexpr int size = 2;
constexpr int data = 3;
}

namespace PlayStatsColumn {
constexpr int id = 0;
constexpr int rating_total = 1;
constexpr int rating_count = 2;
constexpr int trash = 3;
constexpr int played = 4;
constexpr int skipped = 5;
constexpr int duration = 6;
}

namespace PlaylistColumn {
constexpr int id = 0;
constexpr int name = 1;
constexpr int description = 2;
}

namespace PlaylistTrackColumn {
constexpr int track_id = 0;
constexpr int track_order = 1;
}

bool read_file(sqlite3_stmt *row, Epoch000File &file) {
	if(sqlite3_column_type(row, FileColumn::id) != SQLITE_TEXT) {
		log_error("Epoch 0 file has a non-text SHA-1");
		return false;
	}
	if(sqlite3_column_type(row, FileColumn::name) != SQLITE_TEXT) {
		log_error("Epoch 0 file has a non-text name");
		return false;
	}
	if(sqlite3_column_type(row, FileColumn::size) != SQLITE_INTEGER) {
		log_error("Epoch 0 file has a non-integer size");
		return false;
	}
	if(sqlite3_column_type(row, FileColumn::data) != SQLITE_BLOB) {
		log_error("Epoch 0 file has non-blob data");
		return false;
	}

	file.id = sqlite3_column_string(row, FileColumn::id);
	file.name = sqlite3_column_string(row, FileColumn::name);
	file.size = sqlite3_column_int64(row, FileColumn::size);

	const auto *compressed_data = static_cast<const std::byte *>(
		sqlite3_column_blob(row, FileColumn::data)
	);
	const int compressed_size = sqlite3_column_bytes(row, FileColumn::data);
	if(!compressed_data) {
		log_error("Epoch 0 file {} has no compressed data", file.id);
		return false;
	}
	if(compressed_size <= 0) {
		log_error("Epoch 0 file {} has empty compressed data", file.id);
		return false;
	}
	file.compressed_data.assign(compressed_data, compressed_data + compressed_size);
	return true;
}

DecodeFileResult decode_file(
	const Epoch000File &file,
	std::vector<std::byte> &raw,
	ParsedModuleMetadata &metadata
) {
	if(file.size <= 0) {
		log_error("Epoch 0 file {} has an invalid recorded size", file.id);
		return DecodeFileResult::invalid;
	}
	if(static_cast<uint64_t>(file.size) > std::numeric_limits<size_t>::max()) {
		log_error("Epoch 0 file {} is too large for this platform", file.id);
		return DecodeFileResult::invalid;
	}

	const auto frame_size = ZSTD_getFrameContentSize(
		file.compressed_data.data(),
		file.compressed_data.size()
	);
	if(frame_size == ZSTD_CONTENTSIZE_ERROR) {
		log_error("Epoch 0 file {} does not contain a valid Zstd frame", file.id);
		return DecodeFileResult::invalid;
	}
	if(frame_size != ZSTD_CONTENTSIZE_UNKNOWN) {
		if(frame_size != static_cast<uint64_t>(file.size)) {
			log_error("Epoch 0 file {} has a compressed size mismatch", file.id);
			return DecodeFileResult::invalid;
		}
	}

	raw.resize(static_cast<size_t>(file.size));
	const size_t actual_size = ZSTD_decompress(
		raw.data(),
		raw.size(),
		file.compressed_data.data(),
		file.compressed_data.size()
	);
	if(ZSTD_isError(actual_size)) {
		log_error(
			"Epoch 0 file {} could not be decompressed: {}",
			file.id,
			ZSTD_getErrorName(actual_size)
		);
		return DecodeFileResult::invalid;
	}
	if(actual_size != raw.size()) {
		log_error("Epoch 0 file {} does not decompress to its recorded size", file.id);
		return DecodeFileResult::invalid;
	}
	const bool metadata_parsed = parseModMetadata(file.name, raw, metadata);
	if(metadata.sha1 != file.id) {
		log_error("Epoch 0 file {} does not match its SHA-1", file.id);
		return DecodeFileResult::invalid;
	}
	if(!metadata_parsed) {
		log_error("Skipping epoch 0 file {} because its metadata could not be parsed", file.id);
		return DecodeFileResult::metadata_parse_failed;
	}
	return DecodeFileResult::success;
}

bool write_file(
	sqlite3 *destination,
	sqlite3_stmt *insert_file,
	sqlite3_stmt *insert_meta,
	const Epoch000File &file,
	const ParsedModuleMetadata &metadata
) {
	sqlite3_reset(insert_file);
	sqlite3_clear_bindings(insert_file);
	if(sqliteu_bind_string(insert_file, 1, metadata.sha1) != SQLITE_OK) return false;
	if(sqliteu_bind_string(insert_file, 2, metadata.file_name) != SQLITE_OK) return false;
	if(sqlite3_bind_int64(insert_file, 3, metadata.file_size) != SQLITE_OK) return false;
	if(sqlite3_bind_blob(insert_file, 4, file.compressed_data.data(),
			static_cast<int>(file.compressed_data.size()), SQLITE_TRANSIENT) != SQLITE_OK) return false;
	if(sqlite3_step(insert_file) != SQLITE_DONE) {
		log_error("Writing epoch 0 file {} failed: {}", file.id, sqlite3_errmsg(destination));
		return false;
	}

	sqlite3_reset(insert_meta);
	sqlite3_clear_bindings(insert_meta);
	if(sqliteu_bind_string(insert_meta, 1, metadata.sha1) != SQLITE_OK) return false;
	if(sqliteu_bind_string(insert_meta, 2, metadata.md5) != SQLITE_OK) return false;
	if(sqlite3_bind_int64(insert_meta, 3, 0) != SQLITE_OK) return false;
	if(sqliteu_bind_string(insert_meta, 4, metadata.file_name) != SQLITE_OK) return false;
	if(sqlite3_bind_int64(insert_meta, 5, metadata.file_size) != SQLITE_OK) return false;
	if(sqliteu_bind_string(insert_meta, 6, metadata.name) != SQLITE_OK) return false;
	if(sqliteu_bind_string(insert_meta, 7, metadata.type) != SQLITE_OK) return false;
	if(sqlite3_bind_int64(insert_meta, 8, metadata.bpm) != SQLITE_OK) return false;
	if(sqlite3_bind_int64(insert_meta, 9, metadata.duration) != SQLITE_OK) return false;
	if(sqlite3_step(insert_meta) != SQLITE_DONE) {
		log_error("Writing rebuilt metadata for {} failed: {}", file.id,
			sqlite3_errmsg(destination));
		return false;
	}
	return true;
}

bool import_files(TaskControl &tc, sqlite3 *source, sqlite3 *destination) {
	auto status = tc.scope("Validating and importing epoch 0 files");
	SQLITE_FINALIZE sqlite3_stmt *count_files = nullptr;
	if(sqlite3_prepare_v2(source, "SELECT COUNT(*) FROM file",
			-1, &count_files, nullptr) != SQLITE_OK) {
		log_error("Preparing the epoch 0 file count failed: {}", sqlite3_errmsg(source));
		return false;
	}
	if(sqlite3_step(count_files) != SQLITE_ROW) {
		log_error("Counting epoch 0 files failed: {}", sqlite3_errmsg(source));
		return false;
	}
	const int64_t file_count = sqlite3_column_int64(count_files, 0);
	if(file_count < 0) {
		log_error("Epoch 0 file count is invalid: {}", file_count);
		return false;
	}
	const uint64_t total = static_cast<uint64_t>(file_count);
	sqlite3_finalize(count_files);
	count_files = nullptr;
	status.progress(0, total, "files");

	const char *sql = R"(
		SELECT
			id,
			name,
			size,
			data
		FROM file
		ORDER BY id
	)";
	const char *insert_file_sql = R"(
		INSERT OR IGNORE INTO file(id, name, size, data)
		VALUES(?1, ?2, ?3, ?4)
	)";
	const char *insert_meta_sql = R"(
		INSERT OR IGNORE INTO meta(
			id,
			md5,
			todo,
			file_name,
			file_size,
			name,
			type,
			bpm,
			duration
		)
		VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
	)";

	SQLITE_FINALIZE sqlite3_stmt *rows = nullptr;
	if(sqlite3_prepare_v2(source, sql, -1, &rows, nullptr) != SQLITE_OK) {
		log_error("Preparing the epoch 0 file query failed: {}", sqlite3_errmsg(source));
		return false;
	}
	SQLITE_FINALIZE sqlite3_stmt *insert_file = nullptr;
	if(sqlite3_prepare_v2(destination, insert_file_sql, -1, &insert_file, nullptr) != SQLITE_OK) {
		log_error("Preparing the epoch 0 file insert failed: {}", sqlite3_errmsg(destination));
		return false;
	}
	SQLITE_FINALIZE sqlite3_stmt *insert_meta = nullptr;
	if(sqlite3_prepare_v2(destination, insert_meta_sql, -1, &insert_meta, nullptr) != SQLITE_OK) {
		log_error("Preparing the rebuilt metadata insert failed: {}", sqlite3_errmsg(destination));
		return false;
	}

	uint64_t count = 0;
	while(true) {
		if(tc.abort) return false;

		const int result = sqlite3_step(rows);
		if(result == SQLITE_DONE) return true;
		if(result != SQLITE_ROW) {
			log_error("Reading an epoch 0 file failed: {}", sqlite3_errmsg(source));
			return false;
		}

		Epoch000File file;
		if(!read_file(rows, file)) return false;
		std::vector<std::byte> raw;
		ParsedModuleMetadata metadata;
		const auto decode_result = decode_file(file, raw, metadata);
		if(decode_result == DecodeFileResult::invalid) return false;
		if(decode_result == DecodeFileResult::success) {
			if(!write_file(destination, insert_file, insert_meta, file, metadata)) return false;
		}
		status.progress(++count, total, "files");
	}
}

Epoch000PlayStats read_playstats(sqlite3_stmt *row) {
	Epoch000PlayStats playstats;
	playstats.id = sqlite3_column_string(row, PlayStatsColumn::id);
	playstats.rating_total = sqlite3_column_int64(row, PlayStatsColumn::rating_total);
	playstats.rating_count = sqlite3_column_int64(row, PlayStatsColumn::rating_count);
	playstats.trash = sqlite3_column_int64(row, PlayStatsColumn::trash);
	playstats.played = sqlite3_column_int64(row, PlayStatsColumn::played);
	playstats.skipped = sqlite3_column_int64(row, PlayStatsColumn::skipped);
	playstats.duration = sqlite3_column_int64(row, PlayStatsColumn::duration);
	return playstats;
}

bool write_playstats(
	sqlite3 *destination,
	sqlite3_stmt *statement,
	const Epoch000PlayStats &playstats
) {
	sqlite3_reset(statement);
	sqlite3_clear_bindings(statement);

	if(sqliteu_bind_string(statement, 1, playstats.id) != SQLITE_OK) {
		log_error("Binding play-stat ID {} failed: {}", playstats.id, sqlite3_errmsg(destination));
		return false;
	}
	if(sqlite3_bind_int64(statement, 2, playstats.rating_total) != SQLITE_OK) {
		log_error("Binding rating total for {} failed: {}", playstats.id, sqlite3_errmsg(destination));
		return false;
	}
	if(sqlite3_bind_int64(statement, 3, playstats.rating_count) != SQLITE_OK) {
		log_error("Binding rating count for {} failed: {}", playstats.id, sqlite3_errmsg(destination));
		return false;
	}
	if(sqlite3_bind_int64(statement, 4, playstats.trash) != SQLITE_OK) {
		log_error("Binding trash count for {} failed: {}", playstats.id, sqlite3_errmsg(destination));
		return false;
	}
	if(sqlite3_bind_int64(statement, 5, playstats.played) != SQLITE_OK) {
		log_error("Binding played count for {} failed: {}", playstats.id, sqlite3_errmsg(destination));
		return false;
	}
	if(sqlite3_bind_int64(statement, 6, playstats.skipped) != SQLITE_OK) {
		log_error("Binding skipped count for {} failed: {}", playstats.id, sqlite3_errmsg(destination));
		return false;
	}
	if(sqlite3_bind_int64(statement, 7, playstats.duration) != SQLITE_OK) {
		log_error("Binding play duration for {} failed: {}", playstats.id, sqlite3_errmsg(destination));
		return false;
	}
	if(sqlite3_step(statement) != SQLITE_DONE) {
		log_error("Writing play statistics for {} failed: {}", playstats.id,
			sqlite3_errmsg(destination));
		return false;
	}
	return true;
}

bool import_playstats(TaskControl &tc, sqlite3 *source, sqlite3 *destination) {
	auto status = tc.scope("Importing epoch 0 play statistics");
	const char *select_sql = R"(
		SELECT
			id,
			rating_total,
			rating_count,
			trash,
			played,
			skipped,
			duration
		FROM play
	)";
	const char *insert_sql = R"(
		INSERT INTO play(
			id,
			rating_total,
			rating_count,
			trash,
			played,
			skipped,
			duration
		)
		VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)
		ON CONFLICT(id) DO UPDATE SET
			rating_total = rating_total + excluded.rating_total,
			rating_count = rating_count + excluded.rating_count,
			trash = trash + excluded.trash,
			played = played + excluded.played,
			skipped = skipped + excluded.skipped,
			duration = duration + excluded.duration
	)";

	SQLITE_FINALIZE sqlite3_stmt *rows = nullptr;
	if(sqlite3_prepare_v2(source, select_sql, -1, &rows, nullptr) != SQLITE_OK) {
		log_error("Preparing the epoch 0 play-stat query failed: {}", sqlite3_errmsg(source));
		return false;
	}

	SQLITE_FINALIZE sqlite3_stmt *insert = nullptr;
	if(sqlite3_prepare_v2(destination, insert_sql, -1, &insert, nullptr) != SQLITE_OK) {
		log_error("Preparing the epoch 0 play-stat upsert failed: {}",
			sqlite3_errmsg(destination));
		return false;
	}

	uint64_t count = 0;
	while(true) {
		if(tc.abort) return false;

		const int result = sqlite3_step(rows);
		if(result == SQLITE_DONE) return true;
		if(result != SQLITE_ROW) {
			log_error("Reading epoch 0 play statistics failed: {}", sqlite3_errmsg(source));
			return false;
		}

		const Epoch000PlayStats playstats = read_playstats(rows);
		if(!write_playstats(destination, insert, playstats)) return false;
		status.counter(++count, "rows");
	}
}

Epoch000Playlist read_playlist(sqlite3_stmt *row) {
	Epoch000Playlist playlist;
	playlist.id = sqlite3_column_int64(row, PlaylistColumn::id);
	playlist.name = sqlite3_column_string(row, PlaylistColumn::name);
	if(sqlite3_column_type(row, PlaylistColumn::description) != SQLITE_NULL) {
		playlist.description = sqlite3_column_string(row, PlaylistColumn::description);
	}
	return playlist;
}

Epoch000PlaylistTrack read_playlist_track(sqlite3_stmt *row) {
	Epoch000PlaylistTrack track;
	track.track_id = sqlite3_column_string(row, PlaylistTrackColumn::track_id);
	track.track_order = sqlite3_column_int64(row, PlaylistTrackColumn::track_order);
	return track;
}

bool read_playlist_tracks(
	TaskControl &tc,
	sqlite3 *source,
	sqlite3_stmt *rows,
	Epoch000Playlist &playlist
) {
	sqlite3_reset(rows);
	sqlite3_clear_bindings(rows);
	if(sqlite3_bind_int64(rows, 1, playlist.id) != SQLITE_OK) {
		log_error("Binding source playlist {} failed: {}", playlist.id,
			sqlite3_errmsg(source));
		return false;
	}

	while(true) {
		if(tc.abort) return false;

		const int result = sqlite3_step(rows);
		if(result == SQLITE_DONE) return true;
		if(result != SQLITE_ROW) {
			log_error("Reading tracks for playlist {} failed: {}", playlist.id,
				sqlite3_errmsg(source));
			return false;
		}
		playlist.tracks.push_back(read_playlist_track(rows));
	}
}

bool read_playlists(
	TaskControl &tc,
	sqlite3 *source,
	std::vector<Epoch000Playlist> &result
) {
	const char *select_playlists_sql = R"(
		SELECT
			id,
			name,
			description
		FROM playlist
		ORDER BY id
	)";
	const char *select_tracks_sql = R"(
		SELECT
			track_id,
			track_order
		FROM playlist_track
		WHERE playlist_id = ?1
		ORDER BY track_order, id
	)";

	SQLITE_FINALIZE sqlite3_stmt *playlists = nullptr;
	if(sqlite3_prepare_v2(source, select_playlists_sql, -1, &playlists, nullptr) != SQLITE_OK) {
		log_error("Preparing the epoch 0 playlist query failed: {}", sqlite3_errmsg(source));
		return false;
	}

	SQLITE_FINALIZE sqlite3_stmt *tracks = nullptr;
	if(sqlite3_prepare_v2(source, select_tracks_sql, -1, &tracks, nullptr) != SQLITE_OK) {
		log_error("Preparing the epoch 0 playlist-track query failed: {}", sqlite3_errmsg(source));
		return false;
	}

	while(true) {
		if(tc.abort) return false;

		const int step_result = sqlite3_step(playlists);
		if(step_result == SQLITE_DONE) return true;
		if(step_result != SQLITE_ROW) {
			log_error("Reading an epoch 0 playlist failed: {}", sqlite3_errmsg(source));
			return false;
		}

		Epoch000Playlist playlist = read_playlist(playlists);
		if(!read_playlist_tracks(tc, source, tracks, playlist)) return false;
		result.push_back(std::move(playlist));
	}
}

std::optional<int64_t> write_playlist(
	sqlite3 *destination,
	sqlite3_stmt *statement,
	const Epoch000Playlist &playlist
) {
	sqlite3_reset(statement);
	sqlite3_clear_bindings(statement);

	if(sqliteu_bind_string(statement, 1, playlist.name) != SQLITE_OK) {
		log_error("Binding playlist {} name failed: {}", playlist.id, sqlite3_errmsg(destination));
		return std::nullopt;
	}
	if(sqliteu_bind_optional_string(statement, 2, playlist.description) != SQLITE_OK) {
		log_error("Binding playlist {} description failed: {}", playlist.id,
			sqlite3_errmsg(destination));
		return std::nullopt;
	}
	if(sqlite3_step(statement) != SQLITE_DONE) {
		log_error("Writing playlist {} failed: {}", playlist.id, sqlite3_errmsg(destination));
		return std::nullopt;
	}
	return sqlite3_last_insert_rowid(destination);
}

bool write_playlist_track(
	sqlite3 *destination,
	sqlite3_stmt *statement,
	int64_t playlist_id,
	const Epoch000PlaylistTrack &track
) {
	sqlite3_reset(statement);
	sqlite3_clear_bindings(statement);

	if(sqlite3_bind_int64(statement, 1, playlist_id) != SQLITE_OK) {
		log_error("Binding destination playlist {} failed: {}", playlist_id,
			sqlite3_errmsg(destination));
		return false;
	}
	if(sqliteu_bind_string(statement, 2, track.track_id) != SQLITE_OK) {
		log_error("Binding playlist track {} failed: {}", track.track_id,
			sqlite3_errmsg(destination));
		return false;
	}
	if(sqlite3_bind_int64(statement, 3, track.track_order) != SQLITE_OK) {
		log_error("Binding playlist order for {} failed: {}", track.track_id,
			sqlite3_errmsg(destination));
		return false;
	}
	if(sqlite3_step(statement) != SQLITE_DONE) {
		log_error("Writing playlist track {} failed: {}", track.track_id,
			sqlite3_errmsg(destination));
		return false;
	}
	return true;
}

bool write_playlists(
	TaskControl &tc,
	sqlite3 *destination,
	const std::vector<Epoch000Playlist> &playlists,
	TaskControl::Scope &status
) {
	const char *insert_playlist_sql = R"(
		INSERT INTO playlist(name, description)
		VALUES(?1, ?2)
	)";
	const char *insert_track_sql = R"(
		INSERT INTO playlist_track(playlist_id, track_id, track_order)
		VALUES(?1, ?2, ?3)
	)";

	SQLITE_FINALIZE sqlite3_stmt *insert_playlist = nullptr;
	if(sqlite3_prepare_v2(destination, insert_playlist_sql,
			-1, &insert_playlist, nullptr) != SQLITE_OK) {
		log_error("Preparing the epoch 0 playlist insert failed: {}",
			sqlite3_errmsg(destination));
		return false;
	}

	SQLITE_FINALIZE sqlite3_stmt *insert_track = nullptr;
	if(sqlite3_prepare_v2(destination, insert_track_sql, -1, &insert_track, nullptr) != SQLITE_OK) {
		log_error("Preparing the epoch 0 playlist-track insert failed: {}",
			sqlite3_errmsg(destination));
		return false;
	}

	uint64_t count = 0;
	for(const auto &playlist : playlists) {
		if(tc.abort) return false;
		const auto destination_id = write_playlist(destination, insert_playlist, playlist);
		if(!destination_id) return false;
		for(const auto &track : playlist.tracks) {
			if(tc.abort) return false;
			if(!write_playlist_track(destination, insert_track, *destination_id, track)) return false;
		}
		status.counter(++count, "playlists");
	}
	return true;
}

bool import_playlists(TaskControl &tc, sqlite3 *source, sqlite3 *destination) {
	auto status = tc.scope("Importing epoch 0 playlists");
	std::vector<Epoch000Playlist> playlists;
	if(!read_playlists(tc, source, playlists)) return false;
	return write_playlists(tc, destination, playlists, status);
}

} // namespace

bool import_database_epoch_000_to_000(
	TaskControl &tc,
	sqlite3 *source,
	sqlite3 *destination,
	const DatabaseImportOptions &options
) {
	if(options.files) {
		if(!import_files(tc, source, destination)) return false;
	}
	if(options.playstats) {
		if(!import_playstats(tc, source, destination)) return false;
	}
	if(options.playlists) {
		if(!import_playlists(tc, source, destination)) return false;
	}
	return true;
}
