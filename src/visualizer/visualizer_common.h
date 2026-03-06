// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
//
// Internal shared state for the visualizer subsystem.
// Include this in visualizer_*.cpp files only — not in public headers.
#pragma once

#include <array>

#include "glad/glad.h"
// System kissfft installs headers to include/kissfft/; FetchContent exposes the repo root.
#ifdef KISSFFT_SYSTEM_HEADERS
#  include <kissfft/kiss_fftr.h>
#else
#  include <kiss_fftr.h>
#endif

// ─── Constants ───────────────────────────────────────────────────────────────

inline constexpr int FFT_SIZE = 8192;
inline constexpr int NUM_BARS = 256;
inline constexpr int BIN_MIN  = 1;
inline constexpr int BIN_MAX  = FFT_SIZE / 2;

inline constexpr int   HISTORY_SIZE    = 128;   // 3D spectrogram geometry rows
inline constexpr int   HISTORY_SIZE_2D = 512;   // 2D waterfall rows
inline constexpr float GRID_WIDTH    = 9.0f;
inline constexpr float DEPTH_PER_ROW = 0.25f;
inline constexpr float MAX_HEIGHT    = 1.2f;
inline constexpr float MAX_DEPTH     = (HISTORY_SIZE - 1) * DEPTH_PER_ROW;

// ─── Per-bar history ring buffer ─────────────────────────────────────────────

template<int N>
struct BarRingBuffer {
	std::array<std::array<float, NUM_BARS>, N> rows{};
	int head  = 0;
	int count = 0;

	void push(const std::array<float, NUM_BARS>& frame) {
		rows[head] = frame;
		head = (head + 1) % N;
		if (count < N) ++count;
	}
};

// ─── Mode ────────────────────────────────────────────────────────────────────

// To add a new visualizer:
//   1. Add an enum value here.
//   2. Create visualizer_<name>.h/.cpp with init/render/quit functions.
//   3. Add the radio button and dispatch cases in visualizer.cpp.
enum class VisMode { Bars, Spectrogram2D, Spectrogram3D };

// ─── Shared state ────────────────────────────────────────────────────────────

struct VisCommon {
	VisMode mode = VisMode::Bars;

	// Mono accumulation — sliding window fed from the audio ring buffer.
	std::array<float, FFT_SIZE> mono_buf{};
	int mono_count = 0;

	// KissFFT (real-to-halfcomplex)
	kiss_fftr_cfg fft_cfg = nullptr;
	std::array<kiss_fft_cpx, FFT_SIZE / 2 + 1> fft_out{};

	// Per-bar magnitude results:
	//   bar_heights — smoothed (fast-rise/slow-fall), used by Bars mode.
	//   history     — raw per-frame snapshots; capacity HISTORY_SIZE_2D.
	//                 3D Spectrogram reads only the most recent HISTORY_SIZE rows.
	//                 2D Spectrogram reads the full buffer.
	std::array<float, NUM_BARS> bar_heights{};
	BarRingBuffer<HISTORY_SIZE_2D> history;

	// FBO shared across all modes.
	GLuint fbo = 0, fbo_tex = 0, fbo_rbo = 0;
	int fbo_w = 0, fbo_h = 0;
};

// Single instance, defined in visualizer_utils.cpp.
extern VisCommon g_vis;

// ─── Utilities (visualizer_utils.cpp) ────────────────────────────────────────

GLuint compile_shader(GLenum type, const char* src);
GLuint link_program(const char* vert_src, const char* frag_src);
void   recreate_fbo(int w, int h);
void   run_fft();
