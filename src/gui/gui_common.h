// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <format>
#include <array>
#include <chrono>
#include <cmath>


inline std::string formatMs(int ms) {
	using namespace std::chrono;
	auto m = milliseconds(ms);
	std::string res;
	std::format_to(std::back_inserter(res), "{: >2}h {: >2}m {: >2}s",
		duration_cast<hours>(m).count(),
		duration_cast<minutes>(m).count() % 60,
		duration_cast<seconds>(m).count() % 60
	);
	return res;
}

inline std::string fmtSize(int64_t bytes) {
	constexpr std::array<const char*, 4> u {"B", "KB", "MB", "GB"};
	for(size_t i = 0; i < u.size() - 1; i++) {
		if(bytes < 1024) return std::format("{: >4}{}", bytes, u[i]);
		bytes /= 1024;
	}
	return std::format("{: >4}{}", bytes, u.back());
}

inline std::string fmtOptInt(std::optional<int64_t> v) {
	return v.has_value() ? std::to_string(v.value()) : "NULL";
}

inline std::string fmtOptMs(std::optional<int64_t> v) {
	return v.has_value() ? formatMs(static_cast<int>(v.value())) : "NULL";
}

inline std::string fmtOptDouble(std::optional<double> v) {
	return v.has_value() ? std::format("{:.2f}", v.value()) : "NULL";
}

extern bool g_playlist_open_create;
extern bool g_playlist_open_delete;
