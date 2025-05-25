// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <atomic>
#include <format>
#include <iostream>

enum class LogLevel : int32_t {
	debug = 0,
	info  = 1,
	error = 2,
};

extern std::atomic<LogLevel> g_logLevel;

template <typename... T>
void log_internal(const char * file, int line, LogLevel level, std::format_string<T...> f, T &&... p)
{
	if(level >= g_logLevel) {
		std::string s = file;
		std::size_t found = s.find("src");
		std::string foo = s.substr(found + 4);
		
		auto prefix = [](LogLevel l){
			switch(l) {
				default:
				case LogLevel::debug: return "D";
				case LogLevel::info:  return "I";
				case LogLevel::error: return "E";
			}
		}(level);
		
		
		std::cout << std::format("{} {: <24}", prefix, std::format("{}:{}", foo, line)) <<
		    std::format(f, std::forward<T>(p)...) << std::endl;
	}
}


#define log_debug(...) log_internal(__FILE__, __LINE__, LogLevel::debug, __VA_ARGS__)
#define log_info(...)  log_internal(__FILE__, __LINE__, LogLevel::info, __VA_ARGS__)
#define log_error(...) log_internal(__FILE__, __LINE__, LogLevel::error, __VA_ARGS__)


// template <typename E>
// constexpr typename std::underlying_type<E>::type to_underlying(std::atomic<E> &e) noexcept {
// 	E foo = e;
// 	return static_cast<typename std::underlying_type<E>::type>(foo);
// }

inline int log_level_get_int() {
	LogLevel foo = g_logLevel;
	return static_cast<int>(foo);
}
inline void log_level_set_int(int value) {
	g_logLevel = static_cast<LogLevel>(value);
}
