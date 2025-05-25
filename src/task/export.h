// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "../task.h"
#include "../task_util.h"

void export_playlist_run(
	TaskControl &tc,
	sqlite3 *db,
	const std::filesystem::path &directory,
	const std::string &playlist_name,
	const std::vector<ExportTrack> &tracks);
