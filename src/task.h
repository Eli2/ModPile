// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include "global.h"

void task_init(AppState &app);
void task_quit(AppState &app);

void task_stop_current();

std::string task_get_statusline();

struct TaskQueueInfo {
	std::string              current_task;
	std::vector<std::string> queued;
};

TaskQueueInfo task_get_queue_info();
void          task_remove_queued(size_t index);

void task_analyze();
void task_load(std::filesystem::path path);

void task_update_fulltext_search();
void task_mark_tracks_for_analysis();
void task_pull_modland_file_names();
void task_pull_modland_list_supported_formats();
void task_modland_download_missing_files();
void task_import_play(std::filesystem::path path);
void task_export_play(std::filesystem::path path);

struct ExportTrack {
	std::string id;
	std::string file_name;
	std::string name;
	std::string artist;
};

void task_export_playlist(
	std::filesystem::path directory,
	std::string playlist_name,
	std::vector<ExportTrack> tracks);
