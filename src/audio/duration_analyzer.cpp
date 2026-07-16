// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "duration_analyzer.h"

#include <cassert>

PcmAudibleDuration::PcmAudibleDuration(uint32_t sampleRate, uint32_t channels)
	: m_sampleRate(sampleRate), m_channels(channels)
{
	assert(sampleRate > 0);
	assert(channels > 0);
}

void PcmAudibleDuration::add_interleaved(std::span<const int16_t> samples) {
	assert(samples.size() % m_channels == 0);
	for(size_t i = samples.size(); i > 0; --i) {
		if(samples[i - 1] != 0) {
			m_lastAudibleFrame = m_totalFrames + (i - 1) / m_channels + 1;
			break;
		}
	}
	m_totalFrames += samples.size() / m_channels;
}

int64_t PcmAudibleDuration::milliseconds() const {
	const auto wholeSeconds = m_lastAudibleFrame / m_sampleRate;
	const auto remainingFrames = m_lastAudibleFrame % m_sampleRate;
	return static_cast<int64_t>(wholeSeconds * 1000 +
		(remainingFrames * 1000 + m_sampleRate - 1) / m_sampleRate);
}
