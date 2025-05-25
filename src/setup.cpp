// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "setup.h"

#include <SDL3/SDL.h>

#include "glad/glad.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"

#include "config.h"
#include "log.h"

static void show_setup_dialog(AppState &app) {
	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(
		ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
		ImGuiCond_Always,
		ImVec2(0.5f, 0.5f)
	);
	ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Always);
	ImGui::Begin("Welcome to ModPile", nullptr,
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize   |
		ImGuiWindowFlags_NoMove     |
		ImGuiWindowFlags_NoDocking
	);

	ImGui::TextWrapped("No database is configured. Create a new database file or open an existing one.");
	ImGui::Spacing();

	if(!app.setup.error_message.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.3f, 0.3f, 1.f));
		ImGui::TextWrapped("%s", app.setup.error_message.c_str());
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	static SDL_DialogFileFilter filters[] = {{"ModPile Database (*.db)", "db"}};

	if(ImGui::Button("Create New Database...")) {
		app.setup.error_message.clear();
		SDL_ShowSaveFileDialog(
			[](void *userdata, const char * const *filelist, int filter) {
				auto &app = *static_cast<AppState*>(userdata);
				if(!filelist || !*filelist) return;
				Guard g(app.setup.pending_mutex);
				app.setup.pending_path = *filelist;
			},
			&app, app.window, filters, 1, "ModPile.db"
		);
	}

	ImGui::SameLine();

	if(ImGui::Button("Open Existing Database...")) {
		app.setup.error_message.clear();
		SDL_ShowOpenFileDialog(
			[](void *userdata, const char * const *filelist, int filter) {
				auto &app = *static_cast<AppState*>(userdata);
				if(!filelist || !*filelist) return;
				Guard g(app.setup.pending_mutex);
				app.setup.pending_path = *filelist;
			},
			&app, app.window, filters, 1, nullptr, false
		);
	}

	ImGui::End();
}

void setup_iterate(AppState &app) {
	// Consume a path chosen by the file dialog (callback may be on another thread)
	{
		Guard g(app.setup.pending_mutex);
		if(app.setup.pending_path) {
			app.config.database.path = *app.setup.pending_path;
			app.setup.pending_path.reset();
			config_save(app);
			app.setup.active = false;
			// app_full_init() will be called by the caller (AppIterate) on the next check
		}
	}

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	show_setup_dialog(app);

	ImGui::Render();
	ImGuiIO& io = ImGui::GetIO();
	glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
	glClearColor(0.1f, 0.1f, 0.1f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	SDL_GL_SwapWindow(app.window);
}
