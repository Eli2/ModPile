// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_misc.h"

#include "glad/glad.h"
#include "imgui.h"

#include "global.h"
#include "log.h"
#include "task.h"

void gui_config(AppState &app) {
	ImGui::Begin("Config");

	const char* items[] = { "Debug", "Info", "Error"};
	int item_current = log_level_get_int();
	ImGui::Combo("Log level", &item_current, items, IM_ARRAYSIZE(items));
	log_level_set_int(item_current);

	ImGui::End();
}

void gui_indexer(AppState &app) {
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
