// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#pragma once

#include <cstdint>
#include <span>

class PcmAudibleDuration {
public:
	PcmAudibleDuration(uint32_t sampleRate, uint32_t channels);

	void add_interleaved(std::span<const int16_t> samples);
	int64_t milliseconds() const;

private:
	uint32_t m_sampleRate;
	uint32_t m_channels;
	uint64_t m_totalFrames = 0;
	uint64_t m_lastAudibleFrame = 0;
};
