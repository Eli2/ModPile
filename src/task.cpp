// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "task.h"

#include <atomic>
#include <variant>
#include <vector>

#include "blocking_queue.h"
#include "db_common.h"
#include "task/analyze.h"
#include "task/db_import/db_import.h"
#include "task/export.h"
#include "task/import.h"
#include "task/load.h"
#include "task/modland.h"
#include "task/vacuum.h"
#include "task_util.h"
#include "util/sqlite_util.h"
#include "util/thread_util.h"
#include "util/timer_util.h"
#include "db.h"
#include "log.h"


class Poison {
	
};

static bool exec(sqlite3* db, const char* sql) {
	SQLITE_FREE char *msg = nullptr;
	int rc = sqlite3_exec(db, sql, 0, 0, &msg);
	if(rc != SQLITE_OK){
		log_error("SQL error: {} -> {}", sql, msg ? msg : sqlite3_errmsg(db));
		return false;
	}
	return true;
}

struct UpdateFulltextSearchIndex {
	
	void run(TaskControl &tc, sqlite3* db) {
		auto status = tc.scope("Rebuilding full-text search index");
		log_debug("Updating fulltext index ...");
		Timer timer;
		
		SqliteTransaction transaction(db);
		if(!transaction.active()) {
			log_error("BEGIN failed: {}", sqlite3_errmsg(db));
			return;
		}
		auto sql = R"(
			DELETE FROM text_index;
			;
			INSERT INTO text_index(
				id,
				file_name,
				name,
				artist
			)
			SELECT
				meta.id,
				meta.file_name,
				meta.name,
				modland.artist
			FROM
				meta
			LEFT JOIN modland
				ON modland.md5 == meta.md5
			;
		)";
		if(!exec(db, sql)) {
			return;
		}
		if(!transaction.commit()) {
			log_error("COMMIT failed: {}", sqlite3_errmsg(db));
		}

		log_debug("Fulltext done in: {}ms", timer.markMs());
	}
};

struct MarkTracksForAnalysis {
	
	void run(TaskControl &tc, sqlite3* db) {
		auto status = tc.scope("Marking tracks with missing metadata");
		auto sql = R"(
			UPDATE meta
			SET todo=1
			WHERE
				(todo == 0) AND (
					(type IS NULL) OR
					(bpm IS NULL) OR
					(duration IS NULL) OR
					(loudness IS NULL)
				)
			;
		)";
		
		SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
		int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
		if (rc != SQLITE_OK) {
			log_error("prepare failed: {}", sqlite3_errmsg(db));
			return;
		}
		
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			log_error("step failed: {}", sqlite3_errmsg(db));
			return;
		}
	}
};

// ============================================================================



class ModlandPullFilenames {
	

};

struct ModlandListSupportedFormats {
};



struct ModlandDownloadMissingFiles {
	
};

struct Analyze {
	
};

struct Load {
	std::filesystem::path path;
};

struct ImportPlay {
	std::filesystem::path path;
};

struct ImportDatabase {
	std::filesystem::path path;
	DatabaseImportOptions options;
};

struct ExportPlay {
	std::filesystem::path path;
};

struct ExportPlaylist {
	std::filesystem::path directory;
	std::string playlist_name;
	std::vector<ExportTrack> tracks;
};

struct VacuumInto {
	std::filesystem::path path;
};

// ============================================================================

using Tasks = std::variant<
	Poison,
	Load,
	Analyze,
	UpdateFulltextSearchIndex,
	MarkTracksForAnalysis,
	ModlandPullFilenames,
	ModlandListSupportedFormats,
	ModlandDownloadMissingFiles,
	ImportPlay,
	ImportDatabase,
	ExportPlay,
	ExportPlaylist,
	VacuumInto
>;

static queue<Tasks>     g_taskQueue;
static std::thread      g_taskThread;
static TaskControl      g_taskControl;
static LockedString     g_currentTaskName;
static std::mutex       g_taskLifecycleMutex;
static bool             g_taskAccepting = false;
static std::atomic_bool g_databaseImportCompleted = false;

static void enqueue_task(Tasks task) {
	std::lock_guard lock(g_taskLifecycleMutex);
	if(g_taskAccepting) {
		g_taskQueue.push(task);
	}
}

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };

static std::string task_name(const Tasks &task) {
	return std::visit(overload{
		[](const Poison &)                        { return std::string(""); },
		[](const Load &t)                         { return std::format("Load: {}", t.path.filename().string()); },
		[](const Analyze &)                       { return std::string("Analyze tracks"); },
		[](const UpdateFulltextSearchIndex &)     { return std::string("Update fulltext index"); },
		[](const MarkTracksForAnalysis &)         { return std::string("Mark tracks for analysis"); },
		[](const ModlandPullFilenames &)          { return std::string("Modland: pull filenames"); },
		[](const ModlandListSupportedFormats &)   { return std::string("Modland: list formats"); },
		[](const ModlandDownloadMissingFiles &)   { return std::string("Modland: download missing"); },
		[](const ImportPlay &t)                   { return std::format("Import play: {}", t.path.filename().string()); },
		[](const ImportDatabase &t)               { return std::format("Import database: {}", t.path.filename().string()); },
		[](const ExportPlay &t)                   { return std::format("Export play: {}", t.path.filename().string()); },
		[](const ExportPlaylist &t)               { return std::format("Export: {}", t.playlist_name); },
		[](const VacuumInto &t)                   { return std::format("Vacuum into: {}", t.path.filename().string()); },
	}, task);
}

void task_init(AppState &app) {
	g_databaseImportCompleted = false;
	{
		std::lock_guard lock(g_taskLifecycleMutex);
		g_taskQueue.clear();
		g_taskAccepting = false;
	}

	g_taskThread = thread_create("db_task", [&](){
		SQLITE_CLOSE sqlite3* db = db_open(app.config.database.path);
		if(!db) {
			log_error("Failed to open task DB connection");
			return;
		}

		sqlite3_busy_timeout(db, 60*1000);
		
		while(true) {
			auto q = g_taskQueue.pop();

			g_taskControl.abort = false;
			g_taskControl.reset();
			g_currentTaskName.set(task_name(q));
			
			bool exit = false;
			bool failed = false;
			try {
			std::visit(overload{
				[&](Poison &p){
					log_info("Task worker poisoned");
					exit = true;
				},
				[&](Load &t){
					load_run(g_taskControl, db, t.path);
				},
				[&](Analyze &t){
					analyze_run(g_taskControl, db);
				},
				[&](UpdateFulltextSearchIndex &t){
					t.run(g_taskControl, db);
				},
				[&](MarkTracksForAnalysis &h){
					h.run(g_taskControl, db);
				},
				[&](ModlandPullFilenames &h){
					modland_update_index(g_taskControl, db);
				},
				[&](ModlandListSupportedFormats &h){
					modland_probe_formats(g_taskControl, db);
				},
				[&](ModlandDownloadMissingFiles &h){
					modland_download(g_taskControl, db);
				},
				[&](ImportPlay &t){
					import_playstats(g_taskControl, db, t.path);
				},
				[&](ImportDatabase &t){
					if(import_database(g_taskControl, db, t.path, t.options)) {
						g_databaseImportCompleted = true;
					}
				},
				[&](ExportPlay &t){
					export_playstats(g_taskControl, db, t.path);
				},
				[&](ExportPlaylist &t){
					export_playlist_run(g_taskControl, db, t.directory, t.playlist_name, t.tracks);
				},
				[&](VacuumInto &t){
					vacuum_run(g_taskControl, db, t.path);
				}
			}, q);
			} catch(const std::exception &e) {
				failed = true;
				log_error("Task failed with exception: {}", e.what());
				g_taskControl.fail(e.what());
			} catch(...) {
				failed = true;
				log_error("Task failed with unknown exception");
				g_taskControl.fail("Unknown exception");
			}

			g_currentTaskName.set("");
			if(!failed && g_taskControl.snapshot().outcome == TaskStatus::Outcome::Running) {
				if(g_taskControl.abort) g_taskControl.aborted();
				else g_taskControl.succeed();
			}

			if(exit) {
				break;
			}
		}
	});

	{
		std::lock_guard lock(g_taskLifecycleMutex);
		g_taskAccepting = true;
	}
}

void task_quit(AppState &app) {
	g_taskControl.abort = true;
	{
		std::lock_guard lock(g_taskLifecycleMutex);
		g_taskAccepting = false;
		g_taskQueue.clear();
		g_taskQueue.push(Poison());
	}
	if(g_taskThread.joinable())
		g_taskThread.join();
}

void task_stop_current() {
	g_taskControl.abort = true;
}

TaskStatus task_get_status() {
	return g_taskControl.snapshot();
}

TaskQueueInfo task_get_queue_info() {
	TaskQueueInfo info;
	info.current_task = g_currentTaskName.get();
	for(auto &t : g_taskQueue.snapshot()) {
		auto name = task_name(t);
		if(!name.empty())
			info.queued.push_back(std::move(name));
	}
	return info;
}

void task_remove_queued(size_t index) {
	g_taskQueue.remove(index);
}

void task_load(std::filesystem::path path) {
	Load l;
	l.path = path;
	enqueue_task(std::move(l));
}

void task_analyze() {
	enqueue_task(Analyze());
}

void task_update_fulltext_search() {
	enqueue_task(UpdateFulltextSearchIndex());
}

void task_mark_tracks_for_analysis() {
	enqueue_task(MarkTracksForAnalysis());
}


void task_pull_modland_file_names() {
	enqueue_task(ModlandPullFilenames());
}

void task_pull_modland_list_supported_formats() {
	enqueue_task(ModlandListSupportedFormats());
}

void task_modland_download_missing_files() {
	enqueue_task(ModlandDownloadMissingFiles());
}

void task_import_play(std::filesystem::path path) {
	enqueue_task(ImportPlay(std::move(path)));
}

void task_import_database(std::filesystem::path path, DatabaseImportOptions options) {
	enqueue_task(ImportDatabase{std::move(path), options});
}

bool task_consume_database_import_completed() {
	return g_databaseImportCompleted.exchange(false);
}

void task_export_play(std::filesystem::path path) {
	enqueue_task(ExportPlay(std::move(path)));
}

void task_export_playlist(
	std::filesystem::path directory,
	std::string playlist_name,
	std::vector<ExportTrack> tracks)
{
	ExportPlaylist ep;
	ep.directory = std::move(directory);
	ep.playlist_name = std::move(playlist_name);
	ep.tracks = std::move(tracks);
	enqueue_task(std::move(ep));
}

void task_vacuum_into(std::filesystem::path path) {
	enqueue_task(VacuumInto{std::move(path)});
}
