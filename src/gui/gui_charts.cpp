// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_charts.h"
#include "gui_common.h"

#include "imgui.h"

#include "global.h"

void gui_charts(AppState &app) {
	ImGui::Begin("Charts");

	using C = AppState::Charts::Criterion;

	auto tab = [&](const char* label, C criterion) {
		if(ImGui::BeginTabItem(label)) {
			const bool isThis = app.charts.state.active == criterion;
			if(!isThis) {
				app.charts.request.load = criterion;
			}
			if(ImGui::Button("Refresh")) {
				app.charts.request.load = criterion;
			}
			ImGui::SameLine();
			const bool hasRows = isThis && !app.charts.state.rows.empty();
			ImGui::BeginDisabled(!hasRows);
			if(ImGui::Button("Play from start")) {
				auto &first = app.charts.state.rows[0];
				app.player.request.playId.set(first.id);
				app.player.request.playlistTrackId = 1;
				app.player.request.charts_mode = true;
				app.player.request.play = true;
				app.player.request.next = true;
			}
			ImGui::EndDisabled();
			ImGui::EndTabItem();
		}
	};

	if(ImGui::BeginTabBar("##charts")) {
		tab("Top Rated",     C::TopRated);
		tab("Most Played",   C::MostPlayed);
		tab("Most Duration", C::MostDuration);
		ImGui::EndTabBar();
	}

	if(!app.charts.state.active.has_value()) {
		ImGui::End();
		return;
	}

	constexpr auto tblFlags =
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_Reorderable |
		ImGuiTableFlags_Hideable |
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_SizingFixedFit;

	if(ImGui::BeginTable("Charts", 11, tblFlags)) {
		ImGui::TableSetupColumn("play",
			ImGuiTableColumnFlags_NoSort |
			ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_NoHide |
			ImGuiTableColumnFlags_NoHeaderLabel,
			30.f);
		ImGui::TableSetupColumn("file",          ImGuiTableColumnFlags_WidthStretch, 4.f);
		ImGui::TableSetupColumn("size",          ImGuiTableColumnFlags_WidthFixed,  40.f);
		ImGui::TableSetupColumn("artist",        ImGuiTableColumnFlags_WidthStretch, 2.f);
		ImGui::TableSetupColumn("name",          ImGuiTableColumnFlags_WidthStretch, 3.f);
		ImGui::TableSetupColumn("bpm",           ImGuiTableColumnFlags_WidthFixed,   0.f);
		ImGui::TableSetupColumn("duration",      ImGuiTableColumnFlags_WidthFixed,  80.f);
		ImGui::TableSetupColumn("loudness",      ImGuiTableColumnFlags_WidthFixed,   0.f);
		ImGui::TableSetupColumn("rating",        ImGuiTableColumnFlags_WidthFixed,   0.f);
		ImGui::TableSetupColumn("played",        ImGuiTableColumnFlags_WidthFixed,   0.f);
		ImGui::TableSetupColumn("play duration", ImGuiTableColumnFlags_WidthFixed,  80.f);
		ImGui::TableHeadersRow();

		const auto playing_id = app.player.track.id.get();

		for(size_t idx = 0; idx < app.charts.state.rows.size(); ++idx) {
			auto &r = app.charts.state.rows[idx];
			ImGui::TableNextRow();
			if(r.id == playing_id) {
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Header));
			}

			ImGui::TableNextColumn();
			ImGui::PushID(static_cast<int>(idx));
			if(ImGui::Button("play")) {
				app.player.request.playId.set(r.id);
				app.player.request.playlistTrackId = static_cast<int64_t>(idx + 1);
				app.player.request.charts_mode = true;
				app.player.request.play = true;
				app.player.request.next = true;
			}
			ImGui::PopID();

			ImGui::TableNextColumn(); ImGui::Text("%s", r.file_name.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtSize(r.file_size).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", r.artist.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", r.name.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptInt(r.bpm).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptMs(r.duration).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptDouble(r.loudness).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptDouble(r.rating).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptInt(r.played).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptMs(r.play_duration).c_str());
		}

		ImGui::EndTable();
	}

	ImGui::End();
}
