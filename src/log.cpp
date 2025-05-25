// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "log.h"

#include <atomic>

std::atomic<LogLevel> g_logLevel = LogLevel::info;
