// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_misc.h"

#include <algorithm>
#include <cmath>
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

void gui_equalizer(AppState &app) {
	ImGui::Begin("Equalizer");

	{
		bool enabled = app.player.state.eq_enabled.load();
		if(ImGui::Checkbox("EQ", &enabled)) {
			app.player.state.eq_enabled.store(enabled);
		}
		ImGui::SameLine();
		if(ImGui::Button("Reset")) {
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
