// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "xmp_util.h"

const char* xmpu_errstr(int err) {
	switch(err) {
		case -XMP_ERROR_INTERNAL: return "Internal error";
		case -XMP_ERROR_FORMAT:   return "Unsupported module format";
		case -XMP_ERROR_LOAD:     return "Error loading file";
		case -XMP_ERROR_DEPACK:   return "Error depacking file";
		case -XMP_ERROR_SYSTEM:   return "System error";
		case -XMP_ERROR_INVALID:  return "Invalid parameter";
		case -XMP_ERROR_STATE:    return "Invalid player state";
		default:                  return "UNKNOWN";
	}
}
