// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Eli2
#include "analyze.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <format>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "audio/duration_analyzer.h"
#include "audio/loudness_analyzer.h"
#include "util/defer_util.h"
#include "util/sqlite_util.h"
#include "util/thread_safe_queue.h"
#include "util/thread_util.h"
#include "util/xmp_util.h"
#include "log.h"

namespace {

constexpr size_t kAnalyzerThreads = 4;

enum class AnalysisStatus { Success, Unsupported, Failed, Aborted };

struct AnalysisResult {
	std::string id;
	std::string name;
	AnalysisStatus status = AnalysisStatus::Failed;
	double loudness = 0.0;
	int64_t audibleDuration = 0;
	std::string error;
};

} // namespace

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

static AnalysisResult analyze_file(const FileRow &file, const std::atomic_bool &abort) {
	AnalysisResult result;
	result.id = file.id;
	result.name = file.name;

	CLEAN_XMP xmp_context xmp = xmp_create_context();
	if(!xmp) {
		result.error = "could not create libxmp context";
		return result;
	}

	int r = xmp_load_module_from_memory(xmp, file.rawData.data(), file.rawData.size());
	if(r < 0) {
		result.status = AnalysisStatus::Unsupported;
		result.error = std::format("libxmp load failed: {}", xmpu_errstr(r));
		return result;
	}
	SCOPE_EXIT(xmp_release_module(xmp););

	constexpr int freq = 48000;
	LoudnessAnalyzer loudnessAnalyzer(freq, 2);
	if(!loudnessAnalyzer.valid()) {
		result.error = std::format("could not initialize loudness analyzer: {}",
			loudnessAnalyzer.error());
		return result;
	}

	r = xmp_start_player(xmp, freq, 0);
	if(r < 0) {
		result.status = AnalysisStatus::Unsupported;
		result.error = std::format("libxmp player start failed: {}", xmpu_errstr(r));
		return result;
	}
	SCOPE_EXIT(xmp_end_player(xmp););
	
	// https://github.com/libxmp/libxmp/blob/master/docs/libxmp.rst#int-xmp_set_playerxmp_context-c-int-param-int-val
	
	// Mixing is done by openal
	r = xmp_set_player(xmp, XMP_PLAYER_MIX, 100);
	if(r) {
		result.error = std::format("could not set libxmp mix level: {}", xmpu_errstr(r));
		return result;
	}
	// Best quality resampler
	r = xmp_set_player(xmp, XMP_PLAYER_INTERP, XMP_INTERP_SPLINE);
	if(r) {
		result.error = std::format("could not set libxmp interpolation: {}", xmpu_errstr(r));
		return result;
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

		if(abort.load()) {
			result.status = AnalysisStatus::Aborted;
			return result;
		}

		const auto *src = static_cast<const int16_t*>(fi.buffer);
		const auto frames = fi.buffer_size / 4;

		totalSamples += frames;
		if(totalSamples > maxSamples) {
			break;
		}

		const auto pcm = std::span<const int16_t>(src, static_cast<size_t>(frames) * 2);
		audibleDuration.add_interleaved(pcm);
		if(!loudnessAnalyzer.add_interleaved(pcm)) {
			result.error = std::format("loudness analysis failed: {}", loudnessAnalyzer.error());
			return result;
		}
	}

	if(abort.load()) {
		result.status = AnalysisStatus::Aborted;
		return result;
	}
	const auto loudness = loudnessAnalyzer.integrated_loudness();
	if(!loudness.has_value()) {
		result.error = std::format("loudness analysis failed: {}", loudnessAnalyzer.error());
		return result;
	}

	result.status = AnalysisStatus::Success;
	result.loudness = loudness.value();
	result.audibleDuration = audibleDuration.milliseconds();
	return result;
}

void analyze_run(TaskControl &tc, sqlite3 *db) {
	auto task_status = tc.scope(std::format("Analyzing tracks ({} workers)", kAnalyzerThreads));
	
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

	task_status.progress(0, rows.size(), "tracks");
	ThreadSafeQueue<FileRow> jobs(kAnalyzerThreads);
	ThreadSafeQueue<AnalysisResult> results;
	std::vector<std::thread> workers;
	workers.reserve(kAnalyzerThreads);
	SCOPE_EXIT(
		jobs.close(true);
		for(auto &worker : workers) {
			if(worker.joinable()) worker.join();
		}
	);
	for(size_t i = 0; i < kAnalyzerThreads; ++i) {
		workers.push_back(thread_create(std::format("Analyze {}", i + 1), [&] {
			while(auto file = jobs.pop()) {
				AnalysisResult result;
				try {
					result = analyze_file(*file, tc.abort);
				} catch(const std::exception &error) {
					result.id = file->id;
					result.name = file->name;
					result.error = std::format("analysis threw an exception: {}", error.what());
				} catch(...) {
					result.id = file->id;
					result.name = file->name;
					result.error = "analysis threw an unknown exception";
				}
				results.push(std::move(result));
			}
		}));
	}

	size_t completed = 0;
	size_t submitted = 0;
	size_t received = 0;
	auto apply_result = [&](AnalysisResult result) {
		switch(result.status) {
			case AnalysisStatus::Success:
				log_debug("Analyzed {}: loudness={}, audible duration={} ms",
					result.name, result.loudness, result.audibleDuration);
				add_result(db, result.id, result.loudness, result.audibleDuration);
				break;
			case AnalysisStatus::Unsupported:
				log_error("Analysis unsupported for {}: {}", result.name, result.error);
				set_todo(db, result.id, -1);
				break;
			case AnalysisStatus::Failed:
				log_error("Analysis failed for {}: {}", result.name, result.error);
				break;
			case AnalysisStatus::Aborted:
				break;
		}
		++received;
		++completed;
		task_status.progress(completed, rows.size(), "tracks");
	};
	auto apply_ready_results = [&] {
		while(auto result = results.try_pop()) {
			apply_result(std::move(*result));
		}
	};

	for(const auto &id : rows) {
		if(tc.abort) break;

		FileRow file;
		if(!db_get_file(db, id, file)) {
			log_error("Failed to get file for analysis: {}", id);
			set_todo(db, id, -1);
			++completed;
			task_status.progress(completed, rows.size(), "tracks");
			continue;
		}

		if(!jobs.push(std::move(file), &tc.abort)) break;
		++submitted;
		apply_ready_results();
	}

	jobs.close(tc.abort.load());
	if(!tc.abort) {
		while(received < submitted) {
			auto result = results.pop();
			if(!result) break;
			apply_result(std::move(*result));
		}
	}
	for(auto &worker : workers) {
		if(worker.joinable()) worker.join();
	}
	apply_ready_results();
}
