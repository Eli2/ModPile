// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui.h"

#include "gui_charts.h"
#include "gui_menu.h"
#include "gui_misc.h"
#include "gui_pile.h"
#include "gui_player.h"
#include "gui_playlist.h"

#include <SDL3/SDL.h>

#include "glad/glad.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "imgui_internal.h"
#include "imgui.h"

#include "visualizer.h"
#include "config.h"
#include "global.h"

#include <filesystem>
#include <fstream>
#include <sstream>

static bool g_gui_inited = false;
static bool g_apply_default_layout = true;

void gui_reset_layout() { g_apply_default_layout = true; }

void gui_init(AppState &app) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = nullptr;

	ImGui::StyleColorsDark();

	const char* glsl_version = "#version 150";
	ImGui_ImplSDL3_InitForOpenGL(app.window, app.gl_context);
	ImGui_ImplOpenGL3_Init(glsl_version);

	auto layoutPath = config_get_layout_path();
	if(std::filesystem::exists(layoutPath)) {
		std::ifstream f(layoutPath, std::ios::binary);
		std::ostringstream ss;
		ss << f.rdbuf();
		std::string data = ss.str();
		ImGui::LoadIniSettingsFromMemory(data.c_str(), data.size());
		g_apply_default_layout = false;
	}

	g_gui_inited = true;
}

void gui_quit(AppState &app) {
	if(!g_gui_inited)
		return;

	auto layoutPath = config_get_layout_path();
	auto layoutDir = layoutPath.parent_path();
	if(!std::filesystem::exists(layoutDir))
		std::filesystem::create_directories(layoutDir);
	size_t iniSize = 0;
	const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
	std::ofstream(layoutPath, std::ios::binary).write(iniData, static_cast<std::streamsize>(iniSize));

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_GL_DestroyContext(app.gl_context);
}

void gui_iterate(AppState &app) {
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	auto dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_AutoHideTabBar);

	if(g_apply_default_layout) {
		g_apply_default_layout = false;

		ImGui::DockBuilderRemoveNodeChildNodes(dockspace);

		{
			ImGuiID rootLeft;
			ImGuiID rootRight;
			ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.3f, &rootLeft, &rootRight);
			{
				ImGuiID leftTop;
				ImGuiID leftBottom;
				ImGui::DockBuilderSplitNode(rootLeft, ImGuiDir_Down, 0.4f, &leftBottom, &leftTop);

				ImGuiID player;
				ImGuiID rating;
				ImGui::DockBuilderSplitNode(leftTop, ImGuiDir_Down, 0.2f, &rating, &player);

				ImGui::DockBuilderDockWindow("Player", player);
				ImGui::DockBuilderDockWindow("Rating", rating);

				ImGui::DockBuilderDockWindow("Task", leftBottom);
				ImGui::DockBuilderDockWindow("Config", leftBottom);
				ImGui::DockBuilderDockWindow("Equalizer", leftBottom);
			}
			{
				ImGui::DockBuilderDockWindow("Pile", rootRight);
				ImGui::DockBuilderDockWindow("Playlist", rootRight);
				ImGui::DockBuilderDockWindow("Charts", rootRight);
				ImGui::DockBuilderDockWindow("Bars", rootRight);
				ImGui::DockBuilderDockWindow("2D Spectrogram", rootRight);
				ImGui::DockBuilderDockWindow("3D Spectrogram", rootRight);
			}
		}
		ImGui::DockBuilderFinish(dockspace);
	}

	gui_menu(app);
	gui_player(app);
	gui_rating(app);
	gui_pile(app);
	gui_playlist(app);
	gui_charts(app);
	gui_config(app);
	gui_indexer(app);
	gui_equalizer(app);
	visualizer_iterate(app);

	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	ImGui::Render();
	ImGuiIO& io = ImGui::GetIO();
	glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
	glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	SDL_GL_SwapWindow(app.window);
}
