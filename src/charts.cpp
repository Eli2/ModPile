// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "charts.h"

#include <sqlite3.h>

#include "db.h"
#include "log.h"
#include "util/sqlite_util.h"

static sqlite3 *g_chartsConnection = nullptr;

static const char* kSqlTopRated = R"(
	SELECT
		meta.id,
		meta.file_name,
		meta.file_size,
		modland.artist,
		meta.name,
		meta.bpm,
		meta.duration,
		meta.loudness,
		play.rating,
		play.played,
		play.duration
	FROM meta
	JOIN play ON play.id = meta.id
	LEFT JOIN modland ON modland.md5 = meta.md5
	WHERE play.rating_count > 0
	ORDER BY play.rating DESC
	LIMIT 500
	;
)";

static const char* kSqlMostPlayed = R"(
	SELECT
		meta.id,
		meta.file_name,
		meta.file_size,
		modland.artist,
		meta.name,
		meta.bpm,
		meta.duration,
		meta.loudness,
		play.rating,
		play.played,
		play.duration
	FROM meta
	JOIN play ON play.id = meta.id
	LEFT JOIN modland ON modland.md5 = meta.md5
	WHERE play.played > 0
	ORDER BY play.played DESC
	LIMIT 500
	;
)";

static const char* kSqlMostDuration = R"(
	SELECT
		meta.id,
		meta.file_name,
		meta.file_size,
		modland.artist,
		meta.name,
		meta.bpm,
		meta.duration,
		meta.loudness,
		play.rating,
		play.played,
		play.duration
	FROM meta
	JOIN play ON play.id = meta.id
	LEFT JOIN modland ON modland.md5 = meta.md5
	WHERE play.duration > 0
	ORDER BY play.duration DESC
	LIMIT 500
	;
)";

static void load_charts(AppState &app, AppState::Charts::Criterion criterion) {
	const char* sql = nullptr;
	switch(criterion) {
	case AppState::Charts::Criterion::TopRated:   sql = kSqlTopRated;   break;
	case AppState::Charts::Criterion::MostPlayed: sql = kSqlMostPlayed; break;
	case AppState::Charts::Criterion::MostDuration: sql = kSqlMostDuration; break;
	}

	SQLITE_FINALIZE sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(g_chartsConnection, sql, -1, &stmt, nullptr);
	if(rc != SQLITE_OK) {
		log_error("charts prepare failed: {}", sqlite3_errmsg(g_chartsConnection));
		return;
	}

	std::vector<AppState::Charts::State::Row> rows;
	std::vector<std::string> track_ids;

	while(true) {
		auto s = sqlite3_step(stmt);
		if(s == SQLITE_DONE) break;
		if(s != SQLITE_ROW) {
			log_error("charts step failed: {}", sqlite3_errmsg(g_chartsConnection));
			break;
		}

		AppState::Charts::State::Row row;
		row.id            = sqlite3_column_string(stmt, 0);
		row.file_name     = sqlite3_column_string(stmt, 1);
		row.file_size     = sqlite3_column_int64(stmt, 2);
		row.artist        = sqlite3_column_string(stmt, 3);
		row.name          = sqlite3_column_string(stmt, 4);
		row.bpm           = sqlite_util_column_int64(stmt, 5);
		row.duration      = sqlite_util_column_int64(stmt, 6);
		row.loudness      = sqlite_util_column_double(stmt, 7);
		row.rating        = sqlite_util_column_double(stmt, 8);
		row.played        = sqlite_util_column_int64(stmt, 9);
		row.play_duration = sqlite_util_column_int64(stmt, 10);

		track_ids.push_back(row.id);
		rows.emplace_back(std::move(row));
	}

	{
		std::lock_guard<std::mutex> g(app.charts.nav.mutex);
		app.charts.nav.active = true;
		app.charts.nav.track_ids = std::move(track_ids);
	}
	app.charts.state.rows = std::move(rows);
	app.charts.state.active = criterion;
}

bool charts_init(AppState &app) {
	g_chartsConnection = db_open(app);
	return g_chartsConnection != nullptr;
}

void charts_iterate(AppState &app) {
	if(!g_chartsConnection) return;

	if(app.charts.request.load.has_value()) {
		auto criterion = *app.charts.request.load;
		app.charts.request.load = std::nullopt;
		load_charts(app, criterion);
	}
}

void charts_quit() {
	if(g_chartsConnection) {
		sqlite3_close(g_chartsConnection);
		g_chartsConnection = nullptr;
	}
}
