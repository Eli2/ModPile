// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_menu.h"
#include "gui_common.h"

#include <SDL3/SDL_dialog.h>

#include <mutex>
#include <optional>

#include "imgui.h"

#include "global.h"
#include "gui.h"
#include "log.h"
#include "task.h"
#include "task/db_import/db_import.h"
#include "visualizer.h"

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

namespace {

struct DatabaseImportDialogState {
	std::mutex mutex;
	std::optional<std::filesystem::path> pending_path;
	std::filesystem::path path;
	std::string error;
	bool open_options = false;
	bool open_error = false;
	DatabaseImportOptions options;
};

DatabaseImportDialogState g_database_import_dialog;

void select_database_import(AppState &app) {
	static const SDL_DialogFileFilter filters[] = {{"ModPile Database (*.db)", "db"}};
	SDL_ShowOpenFileDialog([](void *, const char * const *filelist, int) {
		if(!filelist || !*filelist) return;
		std::lock_guard lock(g_database_import_dialog.mutex);
		g_database_import_dialog.pending_path = std::filesystem::path(*filelist);
	}, nullptr, app.window, filters, 1, nullptr, false);
}

void database_import_dialog(AppState &app) {
	if(task_consume_database_import_completed()) {
		app.playlist.request.reloadPlaylists = true;
		app.pile.request.executeQuery = true;
	}

	std::optional<std::filesystem::path> pending;
	{
		std::lock_guard lock(g_database_import_dialog.mutex);
		pending = std::move(g_database_import_dialog.pending_path);
		g_database_import_dialog.pending_path.reset();
	}
	if(pending) {
		const auto inspection = inspect_database_import(*pending);
		g_database_import_dialog.path = std::move(*pending);
		if(inspection.compatible) {
			g_database_import_dialog.options = {};
			g_database_import_dialog.open_options = true;
		} else {
			g_database_import_dialog.error = inspection.error_message;
			g_database_import_dialog.open_error = true;
		}
	}

	if(g_database_import_dialog.open_options) {
		g_database_import_dialog.open_options = false;
		ImGui::OpenPopup("Import ModPile database");
	}
	if(ImGui::BeginPopupModal("Import ModPile database", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("Select the data to add from:");
		ImGui::TextWrapped("%s", g_database_import_dialog.path.string().c_str());
		ImGui::Spacing();
		ImGui::Checkbox("Files", &g_database_import_dialog.options.files);
		ImGui::Checkbox("Play statistics", &g_database_import_dialog.options.playstats);
		ImGui::Checkbox("Playlists", &g_database_import_dialog.options.playlists);
		const bool any = g_database_import_dialog.options.files
			|| g_database_import_dialog.options.playstats
			|| g_database_import_dialog.options.playlists;
		ImGui::Separator();
		ImGui::BeginDisabled(!any);
		if(ImGui::Button("Import")) {
			task_import_database(g_database_import_dialog.path, g_database_import_dialog.options);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if(ImGui::Button("Abort")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if(g_database_import_dialog.open_error) {
		g_database_import_dialog.open_error = false;
		ImGui::OpenPopup("Database import unavailable");
	}
	if(ImGui::BeginPopupModal("Database import unavailable", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("%s", g_database_import_dialog.error.c_str());
		ImGui::Separator();
		if(ImGui::Button("Close")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

} // namespace

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
			ImGui::SetItemTooltip("Abort the currently running background task.");
			if(ImGui::MenuItem("Analyze tracks")) {
				task_analyze();
			}
			ImGui::SetItemTooltip("Compute loudness (EBU R128) and read metadata for all unanalyzed tracks.");
			if(ImGui::BeginMenu("Database")) {
				if(ImGui::MenuItem("Import database...")) {
					select_database_import(app);
				}
				ImGui::SetItemTooltip("Add selected data from a compatible ModPile database.");
				ImGui::Separator();
				if(ImGui::MenuItem("Import playstats")) {
					SDL_ShowOpenFileDialog([](void *userdata, const char * const *filelist, int) {
						if(filelist && *filelist) {
							task_import_play(std::filesystem::path(*filelist));
						}
					}, nullptr, app.window, nullptr, 0, nullptr, false);
				}
				ImGui::SetItemTooltip("Import play history from a TSV file into the database.");
				if(ImGui::MenuItem("Export playstats")) {
					SDL_ShowSaveFileDialog([](void *userdata, const char * const *filelist, int) {
						if(filelist && *filelist) {
							task_export_play(std::filesystem::path(*filelist));
						}
					}, nullptr, app.window, nullptr, 0, "play.tsv");
				}
				ImGui::SetItemTooltip("Export play history to a TSV file.");
				if(ImGui::MenuItem("Vacuum into...")) {
					SDL_ShowSaveFileDialog([](void *userdata, const char * const *filelist, int) {
						if(filelist && *filelist) {
							task_vacuum_into(std::filesystem::path(*filelist));
						}
					}, nullptr, app.window, nullptr, 0, "ModPile.db");
				}
				ImGui::SetItemTooltip("Write a compacted copy of the database to a new file.");
				if(ImGui::MenuItem("Update fulltext index")) {
					task_update_fulltext_search();
				}
				ImGui::SetItemTooltip("Rebuild the FTS5 full-text search index.");
				if(ImGui::MenuItem("Update missing metadata")) {
					task_mark_tracks_for_analysis();
				}
				ImGui::SetItemTooltip("Queue tracks that are missing metadata for re-analysis.");
				ImGui::EndMenu();
			}
			if(ImGui::BeginMenu("Modland")) {
				if(ImGui::MenuItem("Pull file names")) {
					task_pull_modland_file_names();
				}
				ImGui::SetItemTooltip("Download the Modland file list and update the local index.");
				if(ImGui::MenuItem("List supported formats")) {
					task_pull_modland_list_supported_formats();
				}
				ImGui::SetItemTooltip("Probe each Modland format to determine which ones libxmp can play.");
				if(ImGui::MenuItem("Download missing files")) {
					task_modland_download_missing_files();
				}
				ImGui::SetItemTooltip("Download Modland tracks not yet present in the collection (supported formats only).");
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
		if(ImGui::BeginMenu("Visualizer")) {
			ImGui::MenuItem("Bars",           nullptr, &g_vis_show_bars);
			ImGui::MenuItem("2D Spectrogram", nullptr, &g_vis_show_2d);
			ImGui::MenuItem("3D Spectrogram", nullptr, &g_vis_show_3d);
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("View")) {
			if(ImGui::MenuItem("Reset Layout")) {
				gui_reset_layout();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	database_import_dialog(app);
}
