// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include "global.h"
#include <atomic>

struct TaskControl {
	std::atomic_bool abort = false;
	LockedString     statusline;
	LockedString     statusline2;
};
