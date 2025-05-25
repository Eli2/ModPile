// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <archive.h>

inline void archive_clean(archive **a) {
	archive_read_free(*a);
}
#define ARCHIVE_CLEAN __attribute__((__cleanup__(archive_clean)))
