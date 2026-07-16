// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#include "analyze.h"

#include <cstdint>
#include <span>

#include <ebur128.h>

#include "audio/duration_analyzer.h"
#include "util/defer_util.h"
#include "util/sqlite_util.h"
#include "util/xmp_util.h"
#include "log.h"

static const char* ebur_err_str(int e) {
	switch(e) {
	case EBUR128_SUCCESS:
		return "EBUR128_SUCCESS";
	case EBUR128_ERROR_NOMEM:
		return "EBUR128_ERROR_NOMEM";
	case EBUR128_ERROR_INVALID_MODE:
		return "EBUR128_ERROR_INVALID_MODE";
	case EBUR128_ERROR_INVALID_CHANNEL_INDEX:
		return "EBUR128_ERROR_INVALID_CHANNEL_INDEX";
	case EBUR128_ERROR_NO_CHANGE:
		return "EBUR128_ERROR_NO_CHANGE";
	default:
		return "UNKNOWN";
	}
}

static void set_todo(sqlite3* db, const std::string &id, const int todo) {
	auto sql = R"(
		UPDATE meta
		SET
			todo = ?2
		WHERE
			id = ?1
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
	if(rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return;
	}
	
	sqliteu_bind_string(stmt, 1, id);
	sqlite3_bind_int64 (stmt, 2, todo);
	
	rc = sqlite3_step(stmt);
	if(rc != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return;
	}
}

struct FileMeta {
	std::string name = "";
	std::string type = "";
	long bpm = 0;
	long duration = 0;
	double loudness = 0.0;
};

static void add_result(sqlite3* db, const std::string &id, double loudness, int64_t audibleDuration) {
	
	auto sql = R"(
		UPDATE meta
		SET
			loudness = ?2,
			audible_duration = ?3
		WHERE
			id = ?1
		;
	)";
	
	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int r = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
	if(r != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(db));
		return;
	}
	
	sqliteu_bind_string(stmt, 1, id);
	sqlite3_bind_double(stmt, 2, loudness);
	sqlite3_bind_int64(stmt, 3, audibleDuration);
	
	r = sqlite3_step(stmt);
	if(r != SQLITE_DONE) {
		log_error("step failed: {}", sqlite3_errmsg(db));
		return;
	}
}

static void analyze(TaskControl &tc, sqlite3* db, std::string &id, std::vector<std::byte> &data) {
	
	int r;
	
	CLEAN_XMP xmp_context xmp = xmp_create_context();
	
	r = xmp_load_module_from_memory(xmp, data.data(), data.size());
	if(r < 0) {
		log_error("xmp err: {} -> {}", id.c_str(), xmpu_errstr(r));
		set_todo(db, id, -1);
		return;
	}
	SCOPE_EXIT(xmp_release_module(xmp););
	
	int freq = 48000;
	
	auto ebur = ebur128_init(2, freq, EBUR128_MODE_I);
	if(!ebur) {
		log_error("Could not create ebur128_state!");
		return;
	}
	SCOPE_EXIT(ebur128_destroy(&ebur););
	
	
	if(xmp_start_player(xmp, freq, 0) < 0) {
		log_error("xmp_start_player failed for: {}", id);
		set_todo(db, id, -1);
		return;
	}
	
	// https://github.com/libxmp/libxmp/blob/master/docs/libxmp.rst#int-xmp_set_playerxmp_context-c-int-param-int-val
	
	// Mixing is done by openal
	r = xmp_set_player(xmp, XMP_PLAYER_MIX, 100);
	if(r) {
		log_error("XMP error: {}", xmpu_errstr(r));
	}
	// Best quality resampler
	r = xmp_set_player(xmp, XMP_PLAYER_INTERP, XMP_INTERP_SPLINE);
	if(r) {
		log_error("XMP error: {}", xmpu_errstr(r));
	}
	
	// Cap at 10 minutes to handle modules with infinite pattern loops (E6x etc.)
	// that never reach the end of the order list and thus never increment loop_count.
	const long maxSamples = 10L * 60 * freq;
	long totalSamples = 0;
	int lastLoopCount = 0;
	PcmAudibleDuration audibleDuration(freq, 2);

	while(xmp_play_frame(xmp) == 0) {
		xmp_frame_info fi;
		xmp_get_frame_info(xmp, &fi);

		if(fi.loop_count != lastLoopCount) {
			break;
		}
		lastLoopCount = fi.loop_count;

		if(tc.abort) {
			return;
		}

		const auto* src = (short*)fi.buffer;
		const auto frames = fi.buffer_size / 4;

		totalSamples += frames;
		if(totalSamples > maxSamples) {
			log_debug("Duration cap reached, stopping analysis");
			break;
		}

		audibleDuration.add_interleaved(
			std::span<const int16_t>(src, static_cast<size_t>(frames) * 2));

		r = ebur128_add_frames_short(ebur, src, frames);
		if(r != EBUR128_SUCCESS) {
			log_error("Ebur error {}", ebur_err_str(r));
		}
	}
	
	double loudness;
	r = ebur128_loudness_global(ebur, &loudness);
	if(r != EBUR128_SUCCESS) {
		log_error("Ebur error {}", ebur_err_str(r));
		return;
	}
	
	log_debug("Loudness: {}, audible duration: {} ms", loudness, audibleDuration.milliseconds());
	add_result(db, id, loudness, audibleDuration.milliseconds());
}

void analyze_run(TaskControl &tc, sqlite3 *db) {
	auto task_status = tc.scope("Analyzing tracks");
	
	std::vector<std::string> rows;
	{
		auto sql = R"(
			SELECT
				meta.id
			FROM meta
			WHERE
				((meta.loudness IS NULL) OR (meta.audible_duration IS NULL))
				AND (meta.todo != -1)
			;
		)";
		
		SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
		int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
		if (rc != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(db));
			return;
		}
		
		do {
			auto s = sqlite3_step(stmt);
			if(s == SQLITE_DONE) {
				break;
			}
			if(s != SQLITE_ROW) {
				log_error("step failed: {}", sqlite3_errmsg(db));
				break;
			}
			
			auto id = sqlite3_column_string(stmt, 0);
			rows.push_back(id);
		} while(!tc.abort);
	}
	
	for(int i = 0; auto & id: rows) {
		i++;
		task_status.progress(i, rows.size(), "tracks");
		if(tc.abort) {
			break;
		}
		
		FileRow file;
		if(!db_get_file(db, id, file)) {
			log_error("Failed to get file for analysis: {}", id);
			set_todo(db, id, -1);
			continue;
		}

		log_debug("Analyzing: {}", file.name);
		auto file_status = tc.scope(file.name);
		analyze(tc, db, file.id, file.rawData);
	}
}
