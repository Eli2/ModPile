// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
//
// Main dispatch for the visualizer subsystem.
// To add a new visualizer:
//   1. Create visualizer_<name>.h/.cpp with init/render/quit functions.
//   2. Add a static VisFbo and an ImGui window below.
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

// ─── Per-window FBOs ─────────────────────────────────────────────────────────

static VisFbo g_fbo_bars;
static VisFbo g_fbo_2d;
static VisFbo g_fbo_3d;

// ─── Helper: render into FBO and display in current ImGui window ──────────────

enum class DepthTest { Off, On };

template<typename RenderFn>
static void vis_render_to_fbo(VisFbo& fbo, DepthTest depth, RenderFn render) {
	ImVec2 content = ImGui::GetContentRegionAvail();
	int w = static_cast<int>(content.x);
	int h = static_cast<int>(content.y);
	if (w <= 0 || h <= 0) return;

	if (w != fbo.w || h != fbo.h)
		recreate_fbo(fbo, w, h);

	GLint prev_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
	GLint prev_vp[4] = {};
	glGetIntegerv(GL_VIEWPORT, prev_vp);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
	glViewport(0, 0, w, h);
	glClearColor(0.04f, 0.04f, 0.10f, 1.0f);

	if (depth == DepthTest::On) {
		glEnable(GL_DEPTH_TEST);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	} else {
		glClear(GL_COLOR_BUFFER_BIT);
	}

	render(w, h);

	if (depth == DepthTest::On)
		glDisable(GL_DEPTH_TEST);

	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
	glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

	ImGui::Image(
		static_cast<ImTextureID>(static_cast<intptr_t>(fbo.fbo_tex)),
		content,
		ImVec2(0, 1), ImVec2(1, 0));
}

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
	static std::array<float, FFT_SIZE * 2> batch;
	size_t got      = app.visualizer.sample_queue.pop(batch.data(), batch.size());
	int    new_mono = static_cast<int>(got / 2);

	if (new_mono > 0) {
		static std::array<float, FFT_SIZE> mono_new;
		for (int i = 0; i < new_mono; ++i)
			mono_new[i] = (batch[i * 2] + batch[i * 2 + 1]) * 0.5f;

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

	// 3. One ImGui window per visualizer — all rendered every frame

	ImGui::Begin("Bars");
	vis_render_to_fbo(g_fbo_bars, DepthTest::Off, [](int w, int h) { bars_render(w, h); });
	ImGui::End();

	ImGui::Begin("2D Spectrogram");
	vis_render_to_fbo(g_fbo_2d, DepthTest::Off, [](int, int) { spectrogram2d_render(); });
	ImGui::End();

	ImGui::Begin("3D Spectrogram");
	vis_render_to_fbo(g_fbo_3d, DepthTest::On, [](int w, int h) { spectrogram3d_render(w, h); });
	ImGui::End();
}

void visualizer_quit(AppState& /*app*/) {
	bars_quit();
	spectrogram2d_quit();
	spectrogram3d_quit();

	destroy_fbo(g_fbo_bars);
	destroy_fbo(g_fbo_2d);
	destroy_fbo(g_fbo_3d);

	if (g_vis.fft_cfg) {
		free(g_vis.fft_cfg);
		g_vis.fft_cfg = nullptr;
	}
}
