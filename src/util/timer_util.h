// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2

#include <chrono>

class Timer {
	std::chrono::time_point<std::chrono::high_resolution_clock> start;
public:
	Timer() {
		start = std::chrono::high_resolution_clock::now();
	}
	long markMs() {
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
		return duration.count();
	}
};
