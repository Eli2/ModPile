// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_player.h"
#include "gui_common.h"

#include "glad/glad.h"
#include "imgui.h"

#include "visualizer.h"
#include "global.h"

#include <cmath>

void gui_player(AppState &app) {
	ImGui::Begin(
		"Player",
		nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_AlwaysAutoResize
	);
	{
		auto str = app.player.track.file_name.get();
		ImGui::Text("File: %s", str.c_str());
	}
	{
		auto str = app.player.track.name.get();
		ImGui::Text("Name: %s", str.c_str());
	}

	auto asd = formatMs(app.player.track.length);
	ImGui::Text("%s", asd.c_str());

	{
		static int drag_pos = 0;
		static bool dragging = false;
		int display = dragging ? drag_pos : (int)app.player.track.elapsed;
		ImGui::SliderInt("##Pos", &display, 0, app.player.track.length);
		if(ImGui::IsItemActive()) {
			drag_pos = display;
			dragging = true;
		} else {
			dragging = false;
		}
		if(ImGui::IsItemDeactivatedAfterEdit()) {
			app.player.request.position.store(drag_pos);
		}
	}

	if(ImGui::Button("Prev")) {
		app.player.request.commands.push(AppState::Player::Request::Command::Previous);
	}
	ImGui::SameLine();
	if(ImGui::Button("Play")) {
		app.player.request.commands.push(AppState::Player::Request::Command::Play);
	}
	ImGui::SameLine();
	if(ImGui::Button("Pause")) {
		app.player.request.commands.push(AppState::Player::Request::Command::Pause);
	}
	ImGui::SameLine();
	if(ImGui::Button("Stop")) {
		app.player.request.commands.push(AppState::Player::Request::Command::Stop);
	}
	ImGui::SameLine();
	if(ImGui::Button("Next")) {
		app.player.request.commands.push(AppState::Player::Request::Command::Next);
	}
	ImGui::Separator();

	{
		bool s = app.player.state.shuffle.load();
		if(ImGui::Checkbox("Shuffle", &s)) {
			app.player.state.shuffle = s;
		}
	}

	{
		using Loop = AppState::Player::State::Loop;
		auto loop = app.player.state.loop_status.load();

		bool repeatTrack = (loop == Loop::Track);
		if(ImGui::Checkbox("Repeat Track", &repeatTrack))
			app.player.state.loop_status = repeatTrack ? Loop::Track : Loop::None;

		ImGui::SameLine();

		bool repeatPlaylist = (loop == Loop::Playlist);
		if(ImGui::Checkbox("Repeat Playlist", &repeatPlaylist))
			app.player.state.loop_status = repeatPlaylist ? Loop::Playlist : Loop::None;
	}

	{
		bool skip = app.config.player.skip_trailing_silence.load();
		if(ImGui::Checkbox("Skip trailing silence", &skip)) {
			app.config.player.skip_trailing_silence.store(skip);
		}
	}

	{
		using C = AppState::Charts::Criterion;
		int64_t trackId = app.player.state.current_playlist_track_id.load();
		bool inCharts   = app.player.state.in_charts_mode.load();

		if(trackId != 0 && inCharts) {
			const char *chartName = "Charts";
			if(app.charts.state.active.has_value()) {
				switch(app.charts.state.active.value()) {
					case C::TopRated:    chartName = "Top Rated";    break;
					case C::MostPlayed:  chartName = "Most Played";  break;
					case C::MostDuration:chartName = "Most Duration"; break;
				}
			}
			ImGui::Text("Source: Charts / %s", chartName);
			ImGui::SameLine();
			if(ImGui::Button("x##src")) {
				app.player.state.current_playlist_track_id = 0;
				app.player.state.in_charts_mode = false;
			}
		} else if(trackId != 0) {
			std::string plName = "Playlist";
			auto playingId = app.player.state.current_playing_playlist_id.load();
			for(auto &pl : app.playlist.state.playlists) {
				if(pl.id == playingId) { plName = pl.name; break; }
			}
			ImGui::Text("Source: Playlist / %s", plName.c_str());
			ImGui::SameLine();
			if(ImGui::Button("x##src")) {
				app.player.state.current_playlist_track_id = 0;
			}
		} else {
			ImGui::Text("Source: Pile (random)");
		}
	}
	ImGui::Separator();
	{
		float gain = app.config.player.gain;
		if(ImGui::SliderFloat("Player Gain", &gain, 0.0f, 2.0f)) {
			app.config.player.gain.store(gain);
		}
		ImGui::SameLine();
		if(ImGui::Button("R##pgain")) {
			app.config.player.gain.store(1.0f);
		}
	}
	ImGui::Separator();
	{
		constexpr float minGainDb = -60.0f;
		constexpr float maxGainDb = 20.0f * std::log10(4.0f);

		float gainDb = app.player.track.gain_db.load();
		if(ImGui::SliderFloat("Track Gain", &gainDb, minGainDb, maxGainDb, "%+.1f dB")) {
			app.player.track.gain_db.store(gainDb);
		}
	}
	ImGui::Separator();
	{
		float tl = static_cast<float>(app.config.player.target_loudness);
		if(ImGui::SliderFloat("Target Loudness", &tl, -36.0f, -6.0f, "%.1f LUFS")) {
			app.config.player.target_loudness = tl;
		}
		ImGui::SameLine();
		if(ImGui::Button("R##tl")) {
			app.config.player.target_loudness = -14.0;
		}
	}
	ImGui::Separator();
	{
		float sw = app.config.player.stereo_width.load();
		if(ImGui::SliderFloat("Stereo width", &sw, 0.0f, 1.0f)) {
			app.config.player.stereo_width.store(sw);
		}
		ImGui::SameLine();
		if(ImGui::Button("R##sw")) {
			app.config.player.stereo_width.store(0.4f);
		}
	}
	ImGui::End();
}

void gui_rating(AppState &app) {
	ImGui::Begin(
		"Rating",
		nullptr,
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_AlwaysAutoResize
	);

	const bool hasTrack = !app.player.track.id.get().empty();
	const long currentRating = app.player.track.rating.load();

	ImGui::BeginDisabled(!hasTrack);
	for(long rating = 0; rating <= 9; ++rating) {
		if(rating > 0)
			ImGui::SameLine();

		const bool selected = currentRating == rating;
		if(selected) {
			ImGui::PushStyleColor(
				ImGuiCol_Button,
				ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}

		const std::string label = std::to_string(rating) + "##rating";
		if(ImGui::Button(label.c_str())) {
			app.player.request.rating = rating;
			app.player.track.rating = rating;
		}

		if(selected)
			ImGui::PopStyleColor();
	}

	if(ImGui::Button("Mark as trash")) {
		app.player.request.trash = true;
		app.player.request.commands.push(AppState::Player::Request::Command::Next);
	}
	ImGui::EndDisabled();

	if(!hasTrack) {
		ImGui::SameLine();
		ImGui::TextDisabled("No track playing");
	}

	ImGui::End();
}
