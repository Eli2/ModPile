// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

struct AppState;

void tray_init(AppState &app);
void tray_iterate(AppState &app);
void tray_quit();
