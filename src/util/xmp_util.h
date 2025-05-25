// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <xmp.h>

inline void xmpu_free_context(xmp_context *a) {
	xmp_free_context(*a);
}
#define CLEAN_XMP __attribute__((__cleanup__(xmpu_free_context)))

const char *xmpu_errstr(int err);
