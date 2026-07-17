// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "global.h"

#include <memory>
#include <utility>

void reset_transient_app_state(AppState &app) {
	// AppState is not assignable because it owns mutexes and atomics. Once all
	// subsystem threads are stopped, reconstructing it in place gives every
	// transient field the default declared in AppState without duplicating those
	// defaults here. Only process-lifetime objects and user configuration survive
	// a database switch.
	auto *window = app.window;
	auto *gl_context = app.gl_context;
	auto config = std::move(app.config);

	std::destroy_at(std::addressof(app));
	std::construct_at(std::addressof(app));

	app.window = window;
	app.gl_context = gl_context;
	app.config = std::move(config);
}
