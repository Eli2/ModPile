// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
//
// Main dispatch for the visualizer subsystem.
// To add a new visualizer:
//   1. Add an enum value to VisMode in visualizer_common.h.
//   2. Create visualizer_<name>.h/.cpp with init/render/quit functions.
//   3. Add the radio button and dispatch cases below.
#include "visualizer.h"
#include "visualizer_common.h"
#include "visualizer_bars.h"
#include "visualizer_2d_spectrogram.h"
#include "visualizer_3d_spectrogram.h"

#include "../global.h"
#include "../log.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstring>

// ─── Public API ──────────────────────────────────────────────────────────────

void visualizer_init(AppState& /*app*/) {
	g_vis.fft_cfg = kiss_fftr_alloc(FFT_SIZE, 0, nullptr, nullptr);
	if (!g_vis.fft_cfg) {
		log_error("kiss_fftr_alloc failed");
		return;
	}
	bars_init();
	spectrogram2d_init();
	spectrogram3d_init();
}

void visualizer_iterate(AppState& app) {
	// 1. Drain ring buffer → mono float accumulation
	static std::array<int16_t, FFT_SIZE * 2> batch;
	size_t got     = app.visualizer.sample_queue.pop(batch.data(), batch.size());
	int    new_mono = static_cast<int>(got / 2);

	if (new_mono > 0) {
		static std::array<float, FFT_SIZE> mono_new;
		for (int i = 0; i < new_mono; ++i)
			mono_new[i] = (batch[i * 2] + batch[i * 2 + 1]) * (0.5f / 32768.0f);

		int keep  = std::min(g_vis.mono_count, FFT_SIZE - new_mono);
		int shift = g_vis.mono_count - keep;
		if (shift > 0)
			std::memmove(g_vis.mono_buf.data(), g_vis.mono_buf.data() + shift,
			             static_cast<size_t>(keep) * sizeof(float));
		std::memcpy(g_vis.mono_buf.data() + keep, mono_new.data(),
		            static_cast<size_t>(new_mono) * sizeof(float));
		g_vis.mono_count = std::min(keep + new_mono, FFT_SIZE);
	}

	// 2. Run FFT when we have a full window
	if (g_vis.fft_cfg && g_vis.mono_count == FFT_SIZE)
		run_fft();

	// 3. ImGui window
	ImGui::Begin("Visualizer");

	// Mode selector — add a RadioButton here for each new mode
	if (ImGui::RadioButton("Bars", g_vis.mode == VisMode::Bars))
		g_vis.mode = VisMode::Bars;
	ImGui::SameLine();
	if (ImGui::RadioButton("2D Spectrogram", g_vis.mode == VisMode::Spectrogram2D))
		g_vis.mode = VisMode::Spectrogram2D;
	ImGui::SameLine();
	if (ImGui::RadioButton("3D Spectrogram", g_vis.mode == VisMode::Spectrogram3D))
		g_vis.mode = VisMode::Spectrogram3D;

	ImVec2 content = ImGui::GetContentRegionAvail();
	int w = static_cast<int>(content.x);
	int h = static_cast<int>(content.y);

	if (w > 0 && h > 0) {
		if (w != g_vis.fbo_w || h != g_vis.fbo_h)
			recreate_fbo(w, h);

		// Save GL state
		GLint prev_fbo = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
		GLint prev_vp[4] = {};
		glGetIntegerv(GL_VIEWPORT, prev_vp);

		glBindFramebuffer(GL_FRAMEBUFFER, g_vis.fbo);
		glViewport(0, 0, w, h);
		glClearColor(0.04f, 0.04f, 0.10f, 1.0f);

		// Dispatch to the active mode — add new modes here
		switch (g_vis.mode) {
		case VisMode::Spectrogram2D:
			glClear(GL_COLOR_BUFFER_BIT);
			spectrogram2d_render();
			break;
		case VisMode::Spectrogram3D:
			glEnable(GL_DEPTH_TEST);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			spectrogram3d_render(w, h);
			glDisable(GL_DEPTH_TEST);
			break;
		case VisMode::Bars:
		default:
			glClear(GL_COLOR_BUFFER_BIT);
			bars_render();
			break;
		}

		// Restore GL state
		glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
		glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

		ImGui::Image(
			static_cast<ImTextureID>(static_cast<intptr_t>(g_vis.fbo_tex)),
			content,
			ImVec2(0, 1), ImVec2(1, 0));
	}

	ImGui::End();
}

void visualizer_quit(AppState& /*app*/) {
	bars_quit();
	spectrogram2d_quit();
	spectrogram3d_quit();

	if (g_vis.fbo) {
		glDeleteTextures(1, &g_vis.fbo_tex);
		glDeleteRenderbuffers(1, &g_vis.fbo_rbo);
		glDeleteFramebuffers(1, &g_vis.fbo);
		g_vis.fbo = g_vis.fbo_tex = g_vis.fbo_rbo = 0;
	}
	if (g_vis.fft_cfg) {
		free(g_vis.fft_cfg);
		g_vis.fft_cfg = nullptr;
	}
}
