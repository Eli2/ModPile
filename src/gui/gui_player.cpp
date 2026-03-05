// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_player.h"
#include "gui_common.h"

#include <algorithm>
#include "glad/glad.h"
#include "imgui.h"

#include "visualizer.h"
#include "global.h"

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
		app.player.request.prev = true;
	}
	ImGui::SameLine();
	if(ImGui::Button("Play")) {
		app.player.request.play = true;
	}
	ImGui::SameLine();
	if(ImGui::Button("Pause")) {
		app.player.request.pause = true;
	}
	ImGui::SameLine();
	if(ImGui::Button("Stop")) {
		app.player.request.stop = true;
	}
	ImGui::SameLine();
	if(ImGui::Button("Next")) {
		app.player.request.next = true;
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
		float gain = app.player.state.gain;
		if(ImGui::SliderFloat("Player Gain", &gain, 0.0f, 2.0f)) {
			app.player.state.gain.store(gain);
		}
		ImGui::SameLine();
		if(ImGui::Button("R##pgain")) {
			app.player.state.gain.store(1.0f);
		}
	}
	ImGui::Separator();
	{
		float gain = app.player.track.gain.load();
		if(ImGui::SliderFloat("Track Gain", &gain, 0.0f, 4.0f)) {
			app.player.track.gain.store(gain);
		}
	}
	ImGui::Separator();
	{
		float sw = app.player.state.stereo_width.load();
		if(ImGui::SliderFloat("Stereo width", &sw, 0.0f, 1.0f)) {
			app.player.state.stereo_width.store(sw);
		}
		ImGui::SameLine();
		if(ImGui::Button("R##sw")) {
			app.player.state.stereo_width.store(0.4f);
		}
	}
	ImGui::Separator();
	{
		bool enabled = app.player.state.eq_enabled.load();
		if(ImGui::Checkbox("EQ", &enabled)) {
			app.player.state.eq_enabled.store(enabled);
		}
		ImGui::SameLine();
		if(ImGui::Button("Reset##eq")) {
			app.player.state.eq_low.store(1.0f);
			app.player.state.eq_mid1.store(1.0f);
			app.player.state.eq_mid2.store(1.0f);
			app.player.state.eq_high.store(1.0f);
		}
	}
	{
		struct Band {
			const char*         label;
			const char*         freq;
			std::atomic<float>& state;
		};
		Band bands[] = {
			{"Low",  "~200Hz", app.player.state.eq_low},
			{"Mid1", "~500Hz", app.player.state.eq_mid1},
			{"Mid2", "~3kHz",  app.player.state.eq_mid2},
			{"High", "~4kHz",  app.player.state.eq_high},
		};
		const float sliderHeight = 150.0f;
		for(int i = 0; i < 4; i++) {
			if(i > 0) ImGui::SameLine();
			ImGui::BeginGroup();
			float db = 20.0f * std::log10(bands[i].state.load());
			ImGui::PushID(i);
			if(ImGui::VSliderFloat("##eq", ImVec2(40, sliderHeight), &db, -18.0f, 18.0f, "%.1f")) {
				float gain = std::clamp(std::pow(10.0f, db / 20.0f), 0.126f, 7.943f);
				bands[i].state.store(gain);
			}
			ImGui::PopID();
			ImGui::Text("%s", bands[i].label);
			ImGui::Text("%s", bands[i].freq);
			ImGui::EndGroup();
		}
	}
	ImGui::End();
}
