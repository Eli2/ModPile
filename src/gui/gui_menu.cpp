// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_menu.h"
#include "gui_common.h"

#include <SDL3/SDL_dialog.h>

#include "imgui.h"

#include "global.h"
#include "log.h"
#include "task.h"

static void add_directory_dialog(AppState &app) {
	SDL_ShowOpenFolderDialog([](void *userdata, const char * const *filelist, int filter) {
		if(!filelist) {
			log_error("An error occured: {}", SDL_GetError());
			return;
		} else if(!*filelist) {
			log_debug("No file selected");
			return;
		}

		while(*filelist) {
			task_load(std::filesystem::path(*filelist));
			filelist++;
		}
	}, &app, app.window, nullptr, true);
}

void gui_menu(AppState &app) {
	if(ImGui::BeginMainMenuBar()) {
		if(ImGui::BeginMenu("File")) {
			if(ImGui::MenuItem("Add Directory", "Alt+A")) {
				add_directory_dialog(app);
			}
			ImGui::Separator();
			if(ImGui::MenuItem("Switch Database...")) {
				app.request.switch_database = true;
			}
			ImGui::Separator();
			if(ImGui::MenuItem("Quit", "Alt+F4")) {
				app.request.quit = true;
			}
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("Task")) {
			if(ImGui::MenuItem("Stop current")) {
				task_stop_current();
			}
			if(ImGui::MenuItem("Analyze tracks")) {
				task_analyze();
			}
			if(ImGui::BeginMenu("Database")) {
				if(ImGui::MenuItem("Import playstats")) {
					task_import_play("play.tsv");
				}
				if(ImGui::MenuItem("Update fulltext index")) {
					task_update_fulltext_search();
				}
				if(ImGui::MenuItem("Update missing metadata")) {
					task_mark_tracks_for_analysis();
				}
				ImGui::EndMenu();
			}
			if(ImGui::BeginMenu("Modland")) {
				if(ImGui::MenuItem("Pull file names")) {
					task_pull_modland_file_names();
				}
				if(ImGui::MenuItem("List supported formats")) {
					task_pull_modland_list_supported_formats();
				}
				if(ImGui::MenuItem("Download missing files")) {
					task_modland_download_missing_files();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("Playlist")) {
			if(ImGui::MenuItem("Create playlist")) {
				g_playlist_open_create = true;
			}
			const bool hasCurrentPlaylist = app.playlist.state.current_playlist_id.has_value();
			ImGui::BeginDisabled(!hasCurrentPlaylist);
			if(ImGui::MenuItem("Delete playlist")) {
				g_playlist_open_delete = true;
			}
			ImGui::EndDisabled();
			if(ImGui::MenuItem("Refresh")) {
				app.playlist.request.reloadPlaylists = true;
			}
			ImGui::Separator();
			const bool hasRows = !app.playlist.state.rows.empty();
			ImGui::BeginDisabled(!hasRows);
			if(ImGui::MenuItem("Export")) {
				struct ExportCbData {
					SDL_Window *window;
					std::string playlist_name;
					std::vector<ExportTrack> tracks;
				};
				std::string playlistName = "playlist";
				if(app.playlist.state.current_playlist_id.has_value()) {
					auto id = app.playlist.state.current_playlist_id.value();
					for(auto &pl : app.playlist.state.playlists) {
						if(pl.id == id) { playlistName = pl.name; break; }
					}
				}
				auto *cbd = new ExportCbData();
				cbd->window = app.window;
				cbd->playlist_name = playlistName;
				for(auto &r : app.playlist.state.rows) {
					ExportTrack t;
					t.id        = r.id;
					t.file_name = r.file_name;
					t.name      = r.name;
					t.artist    = r.artist;
					cbd->tracks.push_back(std::move(t));
				}
				SDL_ShowOpenFolderDialog(
					[](void *userdata, const char* const* filelist, int) {
						auto *data = static_cast<ExportCbData*>(userdata);
						if(filelist && *filelist) {
							task_export_playlist(
								std::filesystem::path(*filelist),
								data->playlist_name,
								std::move(data->tracks));
						}
						delete data;
					},
					cbd,
					app.window,
					nullptr,
					false);
			}
			ImGui::EndDisabled();
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}
