// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "gui_misc.h"

#include <algorithm>
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

static void gui_progress_bar_left(float fraction, const std::string &overlay) {
	// ImGui's ProgressBar positions custom text immediately after the filled
	// portion, so the label moves on every update. Draw it ourselves at a fixed
	// inset instead.
	ImGui::ProgressBar(fraction, ImVec2(-1, 0), "");
	const auto barMin = ImGui::GetItemRectMin();
	const auto barMax = ImGui::GetItemRectMax();
	const auto textSize = ImGui::CalcTextSize(overlay.c_str());
	const auto &style = ImGui::GetStyle();
	const ImVec2 textPosition(
		barMin.x + style.FramePadding.x,
		barMin.y + (barMax.y - barMin.y - textSize.y) * 0.5f
	);
	auto *drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(barMin, barMax, true);
	drawList->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), overlay.c_str());
	drawList->PopClipRect();
}

static void gui_task_status_frame(const TaskStatusFrame &frame, int id) {
	ImGui::PushID(id);
	ImGui::TextUnformatted(frame.label.c_str());
	if(frame.progress) {
		const auto &progress = *frame.progress;
		std::string overlay;
		if(progress.total) {
			overlay = std::format("{} / {}", progress.current, *progress.total);
		} else {
			overlay = std::to_string(progress.current);
		}
		if(!progress.unit.empty()) overlay += std::format(" {}", progress.unit);
		if(progress.total && *progress.total > 0) {
			const auto fraction = static_cast<float>(progress.current) /
				static_cast<float>(*progress.total);
			gui_progress_bar_left(std::clamp(fraction, 0.0f, 1.0f), overlay);
		} else {
			ImGui::TextDisabled("%s", overlay.c_str());
		}
	}
	if(!frame.children.empty()) {
		ImGui::Indent(12.0f);
		for(size_t i = 0; i < frame.children.size(); ++i) {
			gui_task_status_frame(frame.children[i], static_cast<int>(i));
		}
		ImGui::Unindent(12.0f);
	}
	ImGui::PopID();
}

void gui_indexer(AppState &app) {
	ImGui::Begin("Task");

	auto info = task_get_queue_info();
	auto status = task_get_status();

	if(!info.current_task.empty()) {
		ImGui::TextUnformatted(info.current_task.c_str());
		ImGui::SameLine();
		if(ImGui::SmallButton("Abort")) {
			task_stop_current();
		}
		for(size_t i = 0; i < status.frames.size(); ++i) {
			gui_task_status_frame(status.frames[i], static_cast<int>(i));
		}
		if(status.outcome == TaskStatus::Outcome::Failed) {
			ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "Failed: %s", status.message.c_str());
		} else if(status.outcome == TaskStatus::Outcome::Aborted) {
			ImGui::TextDisabled("Aborted%s%s", status.message.empty() ? "" : ": ", status.message.c_str());
		}
	} else {
		if(status.outcome == TaskStatus::Outcome::Failed) {
			ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "Failed: %s", status.message.c_str());
		} else if(status.outcome == TaskStatus::Outcome::Aborted) {
			ImGui::TextDisabled("Aborted%s%s", status.message.empty() ? "" : ": ", status.message.c_str());
		} else {
			ImGui::TextDisabled("Idle");
		}
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

	auto settings = app.config.player.equalizer.load();
	bool changed = false;
	{
		changed |= ImGui::Checkbox("EQ", &settings.enabled);
		ImGui::SameLine();
		if(ImGui::Button("Reset")) {
			settings.low_db = settings.mid1_db = settings.mid2_db = settings.high_db = 0.0f;
			changed = true;
		}
	}
	{
		struct Band {
			const char* label;
			const char* freq;
			float&      db;
		};
		Band bands[] = {
			{"Low",  "~200Hz", settings.low_db},
			{"Mid1", "~500Hz", settings.mid1_db},
			{"Mid2", "~3kHz",  settings.mid2_db},
			{"High", "~4kHz",  settings.high_db},
		};
		const float sliderHeight = 150.0f;
		for(int i = 0; i < 4; i++) {
			if(i > 0) ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::PushID(i);
			if(ImGui::VSliderFloat(
				"##eq",
				ImVec2(40, sliderHeight),
				&bands[i].db,
				AppState::Config::Player::EqualizerSettings::min_db,
				AppState::Config::Player::EqualizerSettings::max_db,
				"%+5.1f"))
			{
				changed = true;
			}
			ImGui::PopID();
			ImGui::Text("%s", bands[i].label);
			ImGui::Text("%s", bands[i].freq);
			ImGui::EndGroup();
		}
	}
	if(changed) {
		app.config.player.equalizer.store(settings);
	}

	ImGui::End();
}
