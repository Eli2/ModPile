// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <exception>
#include <functional>
#include <string>
#include <thread>
#include <utility>

void thread_set_name(std::thread &thread, const char* name);
void thread_log_exception(const char *thread_name, const char *message) noexcept;

template<typename F>
std::thread thread_create(std::string thread_name, F &&function) {
	std::thread thread(
		[thread_name, function = std::forward<F>(function)]() mutable noexcept {
			try {
				std::invoke(std::move(function));
			} catch(const std::exception &e) {
				thread_log_exception(thread_name.c_str(), e.what());
			} catch(...) {
				thread_log_exception(thread_name.c_str(), "unknown exception");
			}
		}
	);
	thread_set_name(thread, thread_name.c_str());
	return thread;
}
