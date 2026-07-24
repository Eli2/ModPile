// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "global.h"

#include <memory>

namespace {

template<typename T>
void reset(T &value) {
	std::destroy_at(std::addressof(value));
	std::construct_at(std::addressof(value));
}

} // namespace

void reset_transient_app_state(AppState &app) {
	reset(app.setup);
	reset(app.request);
	reset(app.player);
	reset(app.pile);
	reset(app.playlist);
	reset(app.mpris);
	reset(app.charts);
	reset(app.visualizer);
}
