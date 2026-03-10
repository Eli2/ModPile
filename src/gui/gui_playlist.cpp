// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_playlist.h"
#include "gui_common.h"

#include "imgui.h"

#include "util/str_util.h"
#include "global.h"

bool g_playlist_open_create = false;
bool g_playlist_open_delete = false;

void gui_playlist(AppState &app) {
	ImGui::Begin("Playlist");

	{
		static std::array<char, 64> playlistName;
		static std::array<char, 128> playlistDesc;
		static int64_t deletePlaylistId = 0;

		if(g_playlist_open_create) {
			g_playlist_open_create = false;
			playlistName.fill(0);
			playlistDesc.fill(0);
			ImGui::OpenPopup("Create playlist");
		}

		if(ImGui::BeginPopupModal("Create playlist", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::InputText("Name", playlistName.data(), playlistName.size());
			ImGui::InputText("Description", playlistDesc.data(), playlistDesc.size());
			ImGui::Separator();
			if(ImGui::Button("Create")) {
				AppState::Playlist::Request::CreatePlaylist req;
				req.name = load_string(playlistName);
				auto desc = load_string(playlistDesc);
				if(!desc.empty()) req.description = desc;
				if(!req.name.empty()) {
					app.playlist.request.createPlaylist = req;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if(g_playlist_open_delete && app.playlist.state.current_playlist_id.has_value()) {
			g_playlist_open_delete = false;
			deletePlaylistId = app.playlist.state.current_playlist_id.value();
			ImGui::OpenPopup("Confirm delete playlist");
		}

		if(ImGui::BeginPopupModal("Confirm delete playlist", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			std::string deleteName = "playlist";
			int64_t deleteCount = 0;
			for(auto &pl : app.playlist.state.playlists) {
				if(pl.id == deletePlaylistId) {
					deleteName = pl.name;
					deleteCount = pl.track_count;
					break;
				}
			}
			ImGui::Text("Delete \"%s\" (%lld tracks)?", deleteName.c_str(), static_cast<long long>(deleteCount));
			ImGui::Separator();
			if(ImGui::Button("Delete")) {
				AppState::Playlist::Request::DeletePlaylist req;
				req.id = deletePlaylistId;
				app.playlist.request.deletePlaylist = req;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if(ImGui::Button("Cancel")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	if(app.playlist.state.playlists.empty()) {
		ImGui::TextUnformatted("No playlists. Use the Playlist menu to create one.");
		ImGui::End();
		return;
	}

	if(ImGui::BeginTabBar("##playlists")) {
		for(auto &pl : app.playlist.state.playlists) {
			const bool isCurrent = app.playlist.state.current_playlist_id.has_value() &&
				app.playlist.state.current_playlist_id.value() == pl.id;
			auto label = std::format("{}###{}", pl.name, pl.id);
			if(ImGui::BeginTabItem(label.c_str())) {
				if(!isCurrent) {
					app.playlist.request.loadPlaylist = pl.id;
				}
				const auto playingId = app.player.track.id.get();
				ImGui::BeginDisabled(playingId.empty());
				if(ImGui::Button("Add playing")) {
					AppState::Playlist::Request::AddTrack req;
					req.playlist_id = pl.id;
					req.track_id = playingId;
					app.playlist.request.addTrack = req;
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				const bool hasRows = isCurrent && !app.playlist.state.rows.empty();
				ImGui::BeginDisabled(!hasRows);
				if(ImGui::Button("Play from start")) {
					auto &first = app.playlist.state.rows[0];
					app.player.request.playId.set(first.id);
					app.player.request.playlistTrackId = first.playlist_track_id;
					app.player.request.playlistId = first.playlist_id;
					app.player.request.charts_mode = false;
					app.player.request.play = true;
					app.player.request.next = true;
				}
				ImGui::EndDisabled();
				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}

	if(!app.playlist.state.current_playlist_id.has_value()) {
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

	if(ImGui::BeginTable("Playlist", 10, tblFlags)) {
		ImGui::TableSetupColumn("play",
			ImGuiTableColumnFlags_NoSort |
			ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_NoHide |
			ImGuiTableColumnFlags_NoHeaderLabel,
			30.f);
		ImGui::TableSetupColumn("edit",
			ImGuiTableColumnFlags_NoSort |
			ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_NoHide |
			ImGuiTableColumnFlags_NoHeaderLabel,
			70.f);

		// KEEP IN SYNC WITH db_query
		ImGui::TableSetupColumn("file",     ImGuiTableColumnFlags_WidthStretch, 4.f);
		ImGui::TableSetupColumn("size",     ImGuiTableColumnFlags_WidthFixed, 40.f);
		ImGui::TableSetupColumn("artist",   ImGuiTableColumnFlags_WidthStretch, 2.f);
		ImGui::TableSetupColumn("name",     ImGuiTableColumnFlags_WidthStretch, 3.f);
		ImGui::TableSetupColumn("bpm",      ImGuiTableColumnFlags_WidthFixed, 0.f);
		ImGui::TableSetupColumn("duration", ImGuiTableColumnFlags_WidthFixed, 80.f);
		ImGui::TableSetupColumn("loudness", ImGuiTableColumnFlags_WidthFixed, 0.f);
		ImGui::TableSetupColumn("rating",   ImGuiTableColumnFlags_WidthFixed, 0.f);

		ImGui::TableHeadersRow();

		const auto playing_pl_track_id = app.player.state.current_playlist_track_id.load();
		const auto playing_pl_id       = app.player.state.current_playing_playlist_id.load();
		const bool in_charts           = app.player.state.in_charts_mode.load();

		for(size_t idx = 0; idx < app.playlist.state.rows.size(); ++idx) {
			auto &r = app.playlist.state.rows[idx];
			ImGui::TableNextRow();
			if(!in_charts && r.playlist_track_id == playing_pl_track_id && r.playlist_id == playing_pl_id) {
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header));
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Header));
			}

			ImGui::TableNextColumn();
			auto uniqeId = std::format("{}{}", r.playlist_track_id, r.id);
			ImGui::PushID(uniqeId.c_str());
			if(ImGui::Button("play")) {
				app.player.request.playId.set(r.id);
				app.player.request.playlistTrackId = r.playlist_track_id;
				app.player.request.playlistId = r.playlist_id;
				app.player.request.charts_mode = false;
				app.player.request.play = true;
				app.player.request.next = true;
			};
			ImGui::PopID();

			ImGui::TableNextColumn();
			{
				ImGui::PushID(static_cast<int>(idx));
				const bool canUp   = idx > 0;
				const bool canDown = idx + 1 < app.playlist.state.rows.size();

				ImGui::BeginDisabled(!canUp);
				if(ImGui::ArrowButton("up", ImGuiDir_Up)) {
					const auto &prev = app.playlist.state.rows[idx - 1];
					AppState::Playlist::Request::MoveTrack req;
					req.playlist_track_id = r.playlist_track_id;
					req.playlist_id = r.playlist_id;
					req.from_order = r.track_order;
					req.to_order = prev.track_order;
					app.playlist.request.moveTrack = req;
				}
				ImGui::EndDisabled();

				ImGui::SameLine();
				ImGui::BeginDisabled(!canDown);
				if(ImGui::ArrowButton("down", ImGuiDir_Down)) {
					const auto &next = app.playlist.state.rows[idx + 1];
					AppState::Playlist::Request::MoveTrack req;
					req.playlist_track_id = r.playlist_track_id;
					req.playlist_id = r.playlist_id;
					req.from_order = r.track_order;
					req.to_order = next.track_order;
					app.playlist.request.moveTrack = req;
				}
				ImGui::EndDisabled();

				ImGui::SameLine();
				if(ImGui::Button("x")) {
					AppState::Playlist::Request::RemoveTrack req;
					req.playlist_track_id = r.playlist_track_id;
					req.playlist_id = r.playlist_id;
					req.track_order = r.track_order;
					app.playlist.request.removeTrack = req;
				}
				ImGui::PopID();
			}

			ImGui::TableNextColumn(); ImGui::Text("%s", r.file_name.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtSize(r.file_size).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", r.artist.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", r.name.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptInt(r.bpm).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptMs(r.duration).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptDouble(r.loudness).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", fmtOptDouble(r.rating).c_str());
		}
		ImGui::EndTable();
	}

	ImGui::End();
}
