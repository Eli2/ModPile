// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "loudness_analyzer.h"

#include <cmath>

#include <ebur128.h>

namespace {

static std::string_view error_message(int error) {
	switch(error) {
		case EBUR128_SUCCESS: return {};
		case EBUR128_ERROR_NOMEM: return "out of memory";
		case EBUR128_ERROR_INVALID_MODE: return "invalid analysis mode";
		case EBUR128_ERROR_INVALID_CHANNEL_INDEX: return "invalid channel index";
		case EBUR128_ERROR_NO_CHANGE: return "not enough audio data";
		default: return "unknown libebur128 error";
	}
}

} // namespace

LoudnessAnalyzer::LoudnessAnalyzer(uint32_t sampleRate, uint32_t channels)
	: m_state(ebur128_init(channels, sampleRate, EBUR128_MODE_I)),
	  m_channels(channels)
{
	if(!m_state) m_error = "could not create libebur128 state";
}

LoudnessAnalyzer::~LoudnessAnalyzer() {
	if(m_state) {
		auto *state = static_cast<ebur128_state*>(m_state);
		ebur128_destroy(&state);
		m_state = nullptr;
	}
}

bool LoudnessAnalyzer::valid() const {
	return m_state != nullptr;
}

bool LoudnessAnalyzer::add_interleaved(std::span<const int16_t> samples) {
	if(!m_state) return false;
	if(samples.size() % m_channels != 0) {
		m_error = "PCM sample count is not divisible by the channel count";
		return false;
	}
	const auto frames = samples.size() / m_channels;
	const auto result = ebur128_add_frames_short(
		static_cast<ebur128_state*>(m_state), samples.data(), frames);
	m_error = error_message(result);
	return result == EBUR128_SUCCESS;
}

std::optional<double> LoudnessAnalyzer::integrated_loudness() {
	if(!m_state) return std::nullopt;
	double loudness = 0.0;
	const auto result = ebur128_loudness_global(
		static_cast<ebur128_state*>(m_state), &loudness);
	m_error = error_message(result);
	if(result != EBUR128_SUCCESS) return std::nullopt;
	if(!std::isfinite(loudness)) {
		m_error = "integrated loudness is not finite";
		return std::nullopt;
	}
	return loudness;
}

std::string_view LoudnessAnalyzer::error() const {
	return m_error;
}
