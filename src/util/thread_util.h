// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <exception>
#include <functional>
#include <thread>
#include <utility>

void thread_set_name(std::thread &thread, const char* name);
void thread_log_exception(const char *thread_name, const char *message) noexcept;

template<typename F>
void thread_exception_guard(const char *thread_name, F &&function) noexcept {
	try {
		std::invoke(std::forward<F>(function));
	} catch(const std::exception &e) {
		thread_log_exception(thread_name, e.what());
	} catch(...) {
		thread_log_exception(thread_name, "unknown exception");
	}
}
