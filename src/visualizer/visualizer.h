// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

struct AppState;

inline bool g_vis_show_bars = true;
inline bool g_vis_show_2d   = true;
inline bool g_vis_show_3d   = true;

void visualizer_init(AppState&);
void visualizer_iterate(AppState&);
void visualizer_quit(AppState&);
