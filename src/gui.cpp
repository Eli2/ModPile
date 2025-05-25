// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include <format>
#include "gui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

#include <SDL3/SDL.h>

#include "glad/glad.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"

#include "imgui_internal.h"
#include "charts.h"
#include "config.h"
#include "log.h"
#include "util/str_util.h"
#include "imgui.h"
#include "task.h"
#include "visualizer.h"

static bool g_gui_inited = false;

void gui_init(AppState &app) {
	
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.IniFilename = nullptr;
	
	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();
	
	// Setup Platform/Renderer backends
	const char* glsl_version = "#version 150";
	ImGui_ImplSDL3_InitForOpenGL(app.window, app.gl_context);
	ImGui_ImplOpenGL3_Init(glsl_version);
	
	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	// - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != nullptr);
	
	g_gui_inited = true;
}

void gui_quit(AppState &app) {
	if(!g_gui_inited)
		return;
	
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	
	SDL_GL_DestroyContext(app.gl_context);
}

std::string formatMs(int ms) {
	using namespace std::chrono;
	
	auto m = std::chrono::milliseconds(ms);
	std::string res;
	std::format_to(std::back_inserter(res), "{: >2}h {: >2}m {: >2}s",
		duration_cast<hours>(m).count(),
		duration_cast<minutes>(m).count() % 60,
		duration_cast<seconds>(m).count() % 60
	);
	return res;
}

static void add_directory_dialog(AppState &app) {
	SDL_ShowOpenFolderDialog([](void *userdata, const char * const *filelist, int filter) {
		//auto &app = *static_cast<AppState*>(userdata);
		if(!filelist) {
			log_error("An error occured: {}", SDL_GetError());
			return;
		} else if(!*filelist) {
			log_debug("No file selected");
			return;
		}
		
		while(*filelist) {
			auto file = *filelist;
			task_load(std::filesystem::path(file));
			filelist++;
		}
	}, &app, app.window, nullptr, true);
}



static bool g_playlist_open_create = false;
static bool g_playlist_open_delete = false;

static void gui_doit(AppState &app) {
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
					// TODO Hardcode
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

static void show_player(AppState &app) {
	
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
		int elapsed = app.player.track.elapsed;
		if(ImGui::SliderInt("##Pos", &elapsed, 0, app.player.track.length)) {
			app.player.request.position.store(elapsed);
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
			// Charts mode — show which chart is active
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
			// Static playlist mode — look up the playing playlist name
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

static void show_pile(AppState &app) {
	
	ImGui::Begin("Pile");
	
	static std::array<char, 64> nameQuery;
	if(ImGui::InputText("Name", nameQuery.data(), nameQuery.size())){
		app.pile.state.query.query = load_string(nameQuery);
		app.pile.state.query.offset = 0;
		app.pile.request.executeQuery = true;
	}
	
	if(ImGui::Button("<<")) {
		app.pile.state.query.offset = 0;
		app.pile.request.executeQuery = true;
	}
	ImGui::SameLine();
	if(ImGui::Button("<")) {
		app.pile.state.query.offset -= app.pile.state.query.limit;
		app.pile.request.executeQuery = true;
	}
	ImGui::SameLine();
	if(ImGui::Button(">")) {
		app.pile.state.query.offset += app.pile.state.query.limit;
		app.pile.request.executeQuery = true;
	}
	if(app.pile.state.query.offset < 0) {
		app.pile.state.query.offset = 0;
	}
	
	{
		static int current_item = 0; // index of selected item
		const char* items[] = { "20", "40", "80", "120" };
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100);
		if(ImGui::Combo("Rows", &current_item, items, IM_ARRAYSIZE(items))) {
			switch(current_item) {
				case 0: app.pile.state.query.limit = 20; break;
				case 1: app.pile.state.query.limit = 40; break;
				case 2: app.pile.state.query.limit = 80; break;
				case 3: app.pile.state.query.limit = 120; break;
			};
			app.pile.request.executeQuery = true;
		}
	}
	
	constexpr auto tblFlags =
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_Reorderable |
		ImGuiTableFlags_Hideable |
		ImGuiTableFlags_Sortable |
		ImGuiTableFlags_ScrollY |
		//ImGuiTableFlags_NoSavedSettings |
		// Decorations
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Borders |
		// Sizing Policy
		ImGuiTableFlags_SizingFixedFit
		;
	
	if(ImGui::BeginTable("Meta", 9, tblFlags)) {
		ImGui::TableSetupColumn("play",
			ImGuiTableColumnFlags_NoSort |
			ImGuiTableColumnFlags_WidthFixed |
			ImGuiTableColumnFlags_NoHide |
			ImGuiTableColumnFlags_NoHeaderLabel,
			30.f
		);
		
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
		
		auto specs = ImGui::TableGetSortSpecs();
		if(specs->SpecsDirty) {
			specs->SpecsDirty = false;
			if(specs->SpecsCount > 0) {
				auto spec = specs->Specs[0];
				app.pile.state.query.sortColNr = spec.ColumnIndex + 1;
				if(spec.SortDirection == ImGuiSortDirection_Ascending) {
					app.pile.state.query.order = AppState::Pile::State::Query::Order::asc;
				} else {
					app.pile.state.query.order = AppState::Pile::State::Query::Order::dsc;
				}
			}
			
			app.pile.request.executeQuery = true;
		}
		
		for(auto &r : app.pile.state.response.rows) {
			ImGui::TableNextRow();
			
			ImGui::TableNextColumn();
			ImGui::PushID(r.id.c_str());
			if(ImGui::Button("play")) {
				app.player.request.playId.set(r.id);
				app.player.request.play = true;
				app.player.request.next = true;
			};
			ImGui::PopID();

			// ImGui::TableNextColumn();
			// ImGui::Text("%s", r.id.c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%s", r.file_name.c_str());
			
			auto sizeConv  = [](int64_t bytes) {
				std::array<const char*, 4> u {"B", "KB", "MB", "GB"};
				for(size_t i=0; i<u.size();i++) {
					if(bytes < 1024) {
						return std::format("{: >4}{}", bytes, u[i]);
					}
					bytes /= 1024;
				}
				return std::format("TODO");
			};
			ImGui::TableNextColumn();
			ImGui::Text("%s", sizeConv(r.file_size).c_str());
			
			ImGui::TableNextColumn();
			ImGui::Text("%s", r.artist.c_str());
			
			ImGui::TableNextColumn();
			ImGui::Text("%s", r.name.c_str());
			
			auto bpmConv = [](std::optional<int64_t> &in){
				return !in.has_value() ? "NULL" : std::to_string(in.value());
			};
			ImGui::TableNextColumn();
			ImGui::Text("%s", bpmConv(r.bpm).c_str());
			
			auto durationConv = [](std::optional<int64_t> &in){
				return !in.has_value() ? "NULL" : formatMs(in.value());
			};
			ImGui::TableNextColumn();
			ImGui::Text("%s", durationConv(r.duration).c_str());
			
			auto loudnessConv = [](std::optional<double> &in){
				return !in.has_value() ? "NULL" : std::format("{:.2f}", in.value()) ;
			};
			ImGui::TableNextColumn();
			ImGui::Text("%s", loudnessConv(r.loudness).c_str());
			
			ImGui::TableNextColumn();
			auto ratingStr = loudnessConv(r.rating);
			ImGui::Text("%s", ratingStr.c_str());
			
		}
		ImGui::EndTable();
	}
	
	
	ImGui::End();
}

static void show_playlist(AppState &app) {
	
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
			// Use stable ID (###id) so renaming doesn't reset tab selection
			auto label = std::format("{}###{}", pl.name, pl.id);
			if(ImGui::BeginTabItem(label.c_str())) {
				if(!isCurrent) {
					app.playlist.request.loadPlaylist = pl.id;
				}
				// Per-playlist action buttons
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
		//ImGuiTableFlags_NoSavedSettings |
		// Decorations
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Borders |
		// Sizing Policy
		ImGuiTableFlags_SizingFixedFit
		;
	
	if(ImGui::BeginTable("Meta", 10, tblFlags)) {
		ImGui::TableSetupColumn("play",
								ImGuiTableColumnFlags_NoSort |
									ImGuiTableColumnFlags_WidthFixed |
									ImGuiTableColumnFlags_NoHide |
									ImGuiTableColumnFlags_NoHeaderLabel,
								30.f
								);
		ImGui::TableSetupColumn("edit",
								ImGuiTableColumnFlags_NoSort |
									ImGuiTableColumnFlags_WidthFixed |
									ImGuiTableColumnFlags_NoHide |
									ImGuiTableColumnFlags_NoHeaderLabel,
								70.f
								);
		
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
		
		
		for(size_t idx = 0; idx < app.playlist.state.rows.size(); ++idx) {
			auto &r = app.playlist.state.rows[idx];
			ImGui::TableNextRow();
			
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
				const bool canUp = idx > 0;
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
			
			// ImGui::TableNextColumn();
			// ImGui::Text("%s", r.id.c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%s", r.file_name.c_str());
			
			auto sizeConv  = [](int64_t bytes) {
				std::array<const char*, 4> u {"B", "KB", "MB", "GB"};
				for(size_t i=0; i<u.size();i++) {
					if(bytes < 1024) {
						return std::format("{: >4}{}", bytes, u[i]);
					}
					bytes /= 1024;
				}
				return std::format("TODO");
			};
			ImGui::TableNextColumn();
			ImGui::Text("%s", sizeConv(r.file_size).c_str());
			
			ImGui::TableNextColumn();
			ImGui::Text("%s", r.artist.c_str());
			
			ImGui::TableNextColumn();
			ImGui::Text("%s", r.name.c_str());
			
			auto bpmConv = [](std::optional<int64_t> &in){
				return !in.has_value() ? "NULL" : std::to_string(in.value());
			};
			ImGui::TableNextColumn();
			ImGui::Text("%s", bpmConv(r.bpm).c_str());
			
			auto durationConv = [](std::optional<int64_t> &in){
				return !in.has_value() ? "NULL" : formatMs(in.value());
			};
			ImGui::TableNextColumn();
			ImGui::Text("%s", durationConv(r.duration).c_str());
			
			auto loudnessConv = [](std::optional<double> &in){
				return !in.has_value() ? "NULL" : std::format("{:.2f}", in.value()) ;
			};
			ImGui::TableNextColumn();
			ImGui::Text("%s", loudnessConv(r.loudness).c_str());
			
			ImGui::TableNextColumn();
			auto ratingStr = loudnessConv(r.rating);
			ImGui::Text("%s", ratingStr.c_str());
			
		}
		ImGui::EndTable();
	}
	
	
	ImGui::End();
}

static void show_config(AppState &app) {
	ImGui::Begin("Config");

	const char* items[] = { "Debug", "Info", "Error"};
	int item_current = log_level_get_int();
	ImGui::Combo("Log level", &item_current, items, IM_ARRAYSIZE(items));
	log_level_set_int(item_current);

	ImGui::End();
}

static void show_indexer(AppState &app) {
	ImGui::Begin("Task");

	auto info = task_get_queue_info();

	if(!info.current_task.empty()) {
		ImGui::TextUnformatted(info.current_task.c_str());
		ImGui::SameLine();
		if(ImGui::SmallButton("Abort")) {
			task_stop_current();
		}
		ImGui::TextUnformatted(task_get_statusline().c_str());
	} else {
		ImGui::TextDisabled("Idle");
	}

	if(!info.queued.empty()) {
		ImGui::Separator();
		ImGui::Text("Queued (%zu):", info.queued.size());
		for(size_t i = 0; i < info.queued.size(); i++) {
			ImGui::TextUnformatted(info.queued[i].c_str());
			ImGui::SameLine();
			if(ImGui::SmallButton(std::format("X##{}", i).c_str())) {
				task_remove_queued(i);
			}
		}
	}

	ImGui::End();
}


static void show_charts(AppState &app) {
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

	if(ImGui::BeginTable("ChartsRows", 11, tblFlags)) {
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

		auto sizeConv = [](int64_t bytes) {
			std::array<const char*, 4> u{"B", "KB", "MB", "GB"};
			for(size_t i = 0; i < u.size(); i++) {
				if(bytes < 1024) return std::format("{: >4}{}", bytes, u[i]);
				bytes /= 1024;
			}
			return std::format("TODO");
		};
		auto optInt = [](std::optional<int64_t> &v) {
			return v.has_value() ? std::to_string(v.value()) : std::string("NULL");
		};
		auto optDouble = [](std::optional<double> &v) {
			return v.has_value() ? std::format("{:.2f}", v.value()) : std::string("NULL");
		};
		auto optMs = [](std::optional<int64_t> &v) -> std::string {
			return !v.has_value() ? "NULL" : formatMs(static_cast<int>(v.value()));
		};

		for(size_t idx = 0; idx < app.charts.state.rows.size(); ++idx) {
			auto &r = app.charts.state.rows[idx];
			ImGui::TableNextRow();

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
			ImGui::TableNextColumn(); ImGui::Text("%s", sizeConv(r.file_size).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", r.artist.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", r.name.c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", optInt(r.bpm).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", optMs(r.duration).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", optDouble(r.loudness).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", optDouble(r.rating).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", optInt(r.played).c_str());
			ImGui::TableNextColumn(); ImGui::Text("%s", optMs(r.play_duration).c_str());
		}

		ImGui::EndTable();
	}

	ImGui::End();
}

void gui_iterate(AppState &app) {

	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	auto dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_AutoHideTabBar);

	static auto dockInit = true;
	if(dockInit) {
		dockInit = false;

		ImGui::DockBuilderRemoveNodeChildNodes(dockspace);

		{
			ImGuiID rootLeft;
			ImGuiID rootRight;
			ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.3f, &rootLeft, &rootRight);
			{
				ImGuiID leftTop;
				ImGuiID leftBottom;
				ImGui::DockBuilderSplitNode(rootLeft, ImGuiDir_Down, 0.4f, &leftBottom, &leftTop);

				ImGui::DockBuilderDockWindow("Player", leftTop);

				ImGui::DockBuilderDockWindow("Task", leftBottom);
				ImGui::DockBuilderDockWindow("Config", leftBottom);
			}
			{
				ImGui::DockBuilderDockWindow("Pile", rootRight);
				ImGui::DockBuilderDockWindow("Playlist", rootRight);
				ImGui::DockBuilderDockWindow("Charts", rootRight);
			ImGui::DockBuilderDockWindow("Visualizer", rootRight);
			}
		}
		ImGui::DockBuilderFinish(dockspace);
	}

	gui_doit(app);
	show_player(app);
	show_pile(app);
	show_playlist(app);
	show_charts(app);
	show_config(app);
	show_indexer(app);
	visualizer_iterate(app);

	// Rendering
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	ImGui::Render();
	ImGuiIO& io = ImGui::GetIO();
	glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
	glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	SDL_GL_SwapWindow(app.window);
}
