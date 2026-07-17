// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "query.h"

#include <condition_variable>
#include <optional>

#include "util/sqlite_util.h"
#include "util/thread_util.h"
#include "util/timer_util.h"
#include "db.h"
#include "log.h"

static sqlite3          *g_queryConnection;
static std::atomic_bool  g_queryThreadQuit = false;
static std::thread       g_queryThread;

using Query = AppState::Pile::State::Query;
using Response = AppState::Pile::State::Response;

static std::mutex              g_queryMutex;
static std::condition_variable g_queryCondVar;
static std::optional<Query>    g_queryRequest;
static std::optional<Response> g_queryResponse;
static std::atomic_bool        g_queryCancel = false;


static bool maybeSha1(std::string_view in) {
	
	if(in.length() != 40)
		return false;
	
	return std::all_of(in.begin(), in.end(), ::isxdigit);
}

static void query(const Query &query, Response &response) {
	
	auto orderStr = [](Query::Order id)->std::string_view {
		switch(id) {
		default:
		case Query::Order::asc: return "ASC NULLS LAST";
		case Query::Order::dsc: return "DESC NULLS LAST";
		}
	};

	std::string sortOrderStr;
	for(const auto &spec : query.sortSpecs) {
		if(!sortOrderStr.empty()) sortOrderStr += ", ";
		sortOrderStr += spec.col;
		sortOrderStr += ' ';
		sortOrderStr += orderStr(spec.order);
	}
	if(sortOrderStr.empty()) sortOrderStr = "meta.file_name ASC NULLS LAST";


	std::string sqlAdd = "";
	if(maybeSha1(query.query)) {
		sqlAdd = R"(
			WHERE
				meta.id == ?1
		)";
	} else if(!query.query.empty()) {
		sqlAdd = R"(
			JOIN (
				SELECT * FROM text_index
				WHERE text_index MATCH ?1
			) fts_result ON meta.id = fts_result.id
		)";
	}
	
	auto sql = std::format(R"(
		SELECT
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
			meta
		LEFT JOIN
			play ON play.id == meta.id
		LEFT JOIN
			modland ON modland.md5 == meta.md5
		{}
		ORDER BY
			{}
		LIMIT ?2
		OFFSET ?3
		;
	)",
	sqlAdd,
	sortOrderStr);

	SQLITE_FINALIZE sqlite3_stmt *stmt = NULL;
	int rc = sqlite3_prepare_v2(g_queryConnection, sql.c_str(), -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		log_error("prepare failed: {}", sqlite3_errmsg(g_queryConnection));
		log_error("{}", sql);
		return;
	}
	
	sqlite3_bind_text(stmt,  1, query.query.c_str(), query.query.length(), SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 2, query.limit);
	sqlite3_bind_int64(stmt, 3, query.offset);
	
	
	Timer timer;
	
	while(true) {
		auto s = sqlite3_step(stmt);
		if(s == SQLITE_ERROR || s == SQLITE_MISUSE) {
			log_error("step failed: {}", sqlite3_errmsg(g_queryConnection));
			break;
		}
		if(s == SQLITE_DONE) {
			break;
		}
		if(s != SQLITE_ROW) {
			break;
		}

		Response::Row row;
		row.id        = sqlite3_column_string(stmt, 0);
		row.file_name = sqlite3_column_string(stmt, 1);
		row.file_size = sqlite3_column_int64(stmt,  2);
		row.artist    = sqlite3_column_string(stmt, 3);
		row.name      = sqlite3_column_string(stmt, 4);
		row.bpm       = sqlite_util_column_int64(stmt,  5);
		row.duration  = sqlite_util_column_int64(stmt,  6);
		row.loudness  = sqlite_util_column_double(stmt,  7);
		row.rating    = sqlite_util_column_double(stmt,  8);
		
		
		response.rows.emplace_back(row);
	}
	
	log_debug("Query completed in: {}ms", timer.markMs());
}



void db_query_init(AppState &app) {

	g_queryConnection = db_open(app.config.database.path);
	if(!g_queryConnection) {
		log_error("Failed to open query DB connection");
		return;
	}

	sqlite3_progress_handler(g_queryConnection, 10, [](void *userdata)->int {
		return g_queryCancel;
	}, nullptr);
	
	try {
	g_queryThread = thread_create("Query", [](){
		while(true) {
			Query q;
			{
				std::unique_lock<std::mutex> lock(g_queryMutex);
				g_queryCondVar.wait(lock, []{
					return g_queryThreadQuit || g_queryRequest.has_value();
				});
				if(g_queryThreadQuit) {
					break;
				}
				q = g_queryRequest.value();
				g_queryRequest.reset();
			}
			
			log_debug("Received query: {}", q.query);
			
			g_queryCancel = false;
			Response r;
			try {
				query(q, r);
			} catch(const std::exception &e) {
				log_error("Query failed with exception: {}", e.what());
				continue;
			} catch(...) {
				log_error("Query failed with unknown exception");
				continue;
			}
			
			{
				std::unique_lock lock(g_queryMutex);
				g_queryResponse = r;
			}
		}
	});
	} catch(const std::exception& e) {
		log_error("Failed to create query thread: {}", e.what());
		sqlite3_close(g_queryConnection);
		g_queryConnection = nullptr;
	}
}

void db_query_iterate(AppState &app) {
	
	if(app.pile.request.executeQuery) {
		app.pile.request.executeQuery = false;
		
		g_queryCancel = true;
		{
			std::lock_guard lk(g_queryMutex);
			g_queryRequest = app.pile.state.query;
		}
		g_queryCondVar.notify_one();
	}
	
	{
		std::lock_guard lk(g_queryMutex);
		if(g_queryResponse.has_value()) {
			app.pile.state.response = g_queryResponse.value();
			g_queryResponse.reset();
		}
	}
}

void db_query_quit() {
	g_queryThreadQuit = true;
	g_queryCondVar.notify_one();
	if(g_queryThread.joinable())
		g_queryThread.join();
	g_queryThreadQuit = false;
	sqlite3_close(g_queryConnection);
	g_queryConnection = nullptr;
}
