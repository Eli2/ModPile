// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "thread_util.h"

#include <pthread.h>

#include "../log.h"

void thread_set_name(std::thread &thread, const char* name) {
	
	pthread_setname_np(thread.native_handle(), name);
}

void thread_log_exception(const char *thread_name, const char *message) noexcept {
	try {
		log_error("Unhandled exception in {} thread: {}",
			thread_name ? thread_name : "worker",
			message ? message : "unknown exception");
	} catch(...) {
		// Exception handlers must never allow another exception to escape.
	}
}
