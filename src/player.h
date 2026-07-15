// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

struct AppState;

bool player_init(AppState &app);
bool player_iterate(AppState &app);
void player_quit();
