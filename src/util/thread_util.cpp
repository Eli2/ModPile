// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "thread_util.h"

#include <pthread.h>

void thread_set_name(std::thread &thread, const char* name) {
	
	pthread_setname_np(thread.native_handle(), name);
}
