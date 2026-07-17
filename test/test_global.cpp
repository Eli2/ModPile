// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "../src/global.h"

TEST_CASE("resetting database state drops stale rows navigation and requests", "[global]") {
	AppState app;
	auto *window = reinterpret_cast<SDL_Window*>(0x1);
	auto *gl_context = reinterpret_cast<SDL_GLContextState*>(0x2);
	app.window = window;
	app.gl_context = gl_context;
	app.config.database.path = "/tmp/preserved.db";
	app.config.player.target_loudness = -20.0;

	app.pile.request.executeQuery = true;
	app.pile.state.query.offset = 40;
	app.pile.state.response.rows.emplace_back();

	app.playlist.request.loadPlaylist = 7;
	app.playlist.request.removeTrack =
		AppState::Playlist::Request::RemoveTrack{11, 7, 2};
	app.playlist.state.playlists.emplace_back();
	app.playlist.state.current_playlist_id = 7;
	app.playlist.state.rows.emplace_back();

	app.charts.request.load = AppState::Charts::Criterion::MostPlayed;
	app.charts.state.active = AppState::Charts::Criterion::MostPlayed;
	app.charts.state.rows.emplace_back();
	app.charts.nav.active = true;
	app.charts.nav.track_ids.emplace_back("old-track");

	app.player.request.play = true;
	app.player.request.playId.set("old-track");
	app.player.request.playlistTrackId = 11;
	app.player.request.playlistId = 7;
	app.player.request.charts_mode = true;
	app.player.state.current_playlist_track_id = 11;
	app.player.state.current_playing_playlist_id = 7;
	app.player.state.in_charts_mode = true;
	app.player.track.id.set("old-track");
	app.player.track.file_name.set("old.mod");
	app.player.track.name.set("Old track");
	app.player.track.length = 123;

	reset_transient_app_state(app);

	CHECK_FALSE(app.pile.request.executeQuery);
	CHECK(app.pile.state.query.offset == 0);
	CHECK(app.pile.state.response.rows.empty());

	CHECK_FALSE(app.playlist.request.loadPlaylist.has_value());
	CHECK_FALSE(app.playlist.request.removeTrack.has_value());
	CHECK(app.playlist.state.playlists.empty());
	CHECK_FALSE(app.playlist.state.current_playlist_id.has_value());
	CHECK(app.playlist.state.rows.empty());

	CHECK_FALSE(app.charts.request.load.has_value());
	CHECK_FALSE(app.charts.state.active.has_value());
	CHECK(app.charts.state.rows.empty());
	CHECK_FALSE(app.charts.nav.active);
	CHECK(app.charts.nav.track_ids.empty());

	CHECK_FALSE(app.player.request.play.load());
	CHECK(app.player.request.playId.get().empty());
	CHECK(app.player.request.playlistTrackId.load() == 0);
	CHECK(app.player.request.playlistId.load() == 0);
	CHECK_FALSE(app.player.request.charts_mode.load());
	CHECK(app.player.state.current_playlist_track_id.load() == 0);
	CHECK(app.player.state.current_playing_playlist_id.load() == 0);
	CHECK_FALSE(app.player.state.in_charts_mode.load());
	CHECK(app.player.track.id.get().empty());
	CHECK(app.player.track.file_name.get().empty());
	CHECK(app.player.track.name.get().empty());
	CHECK(app.player.track.length.load() == 0);
	CHECK(app.player.request.seek.load() == std::numeric_limits<int64_t>::min());

	CHECK(app.window == window);
	CHECK(app.gl_context == gl_context);
	CHECK(app.config.database.path == "/tmp/preserved.db");
	CHECK(app.config.player.target_loudness == -20.0);
}
