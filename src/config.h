// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <filesystem>
#include "global.h"

void config_load(AppState &app);
void config_save(AppState &app);

// Returns the path where the ImGui docking layout is stored.
// Valid after config_load() has been called.
std::filesystem::path config_get_layout_path();
