// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

class LoudnessAnalyzer {
public:
	LoudnessAnalyzer(uint32_t sampleRate, uint32_t channels);
	~LoudnessAnalyzer();

	LoudnessAnalyzer(const LoudnessAnalyzer&) = delete;
	LoudnessAnalyzer& operator=(const LoudnessAnalyzer&) = delete;

	bool valid() const;
	bool add_interleaved(std::span<const int16_t> samples);
	std::optional<double> integrated_loudness();
	std::string_view error() const;

private:
	void *m_state = nullptr;
	uint32_t m_channels;
	std::string_view m_error;
};
