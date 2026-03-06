// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2

#include "visualizer_common.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ─── Global state ────────────────────────────────────────────────────────────

VisCommon g_vis;

// ─── GL helpers ──────────────────────────────────────────────────────────────

GLuint compile_shader(GLenum type, const char* src) {
	GLuint id = glCreateShader(type);
	glShaderSource(id, 1, &src, nullptr);
	glCompileShader(id);
	GLint ok = 0;
	glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char buf[512];
		glGetShaderInfoLog(id, sizeof(buf), nullptr, buf);
		log_error("visualizer shader compile error: {}", buf);
	}
	return id;
}

GLuint link_program(const char* vert_src, const char* frag_src) {
	GLuint vert = compile_shader(GL_VERTEX_SHADER,   vert_src);
	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	GLuint prog = glCreateProgram();
	if (!prog) {
		log_error("glCreateProgram failed");
		glDeleteShader(vert);
		glDeleteShader(frag);
		return 0;
	}
	glAttachShader(prog, vert);
	glAttachShader(prog, frag);
	glLinkProgram(prog);
	glDeleteShader(vert);
	glDeleteShader(frag);
	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char buf[512];
		glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
		log_error("visualizer shader link error: {}", buf);
	}
	return prog;
}

void destroy_fbo(VisFbo& f) {
	if (f.fbo) {
		glDeleteTextures(1, &f.fbo_tex);
		glDeleteRenderbuffers(1, &f.fbo_rbo);
		glDeleteFramebuffers(1, &f.fbo);
		f = {};
	}
}

void recreate_fbo(VisFbo& f, int w, int h) {
	destroy_fbo(f);

	glGenFramebuffers(1, &f.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);

	glGenTextures(1, &f.fbo_tex);
	glBindTexture(GL_TEXTURE_2D, f.fbo_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f.fbo_tex, 0);

	glGenRenderbuffers(1, &f.fbo_rbo);
	glBindRenderbuffer(GL_RENDERBUFFER, f.fbo_rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, f.fbo_rbo);

	GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (fbStatus != GL_FRAMEBUFFER_COMPLETE)
		log_error("Framebuffer incomplete: 0x{:X}", static_cast<unsigned>(fbStatus));

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	f.w = w;
	f.h = h;
}

// ─── FFT + history ───────────────────────────────────────────────────────────

void run_fft() {
	// Apply Hann window
	static std::array<kiss_fft_scalar, FFT_SIZE> windowed;
	for (int i = 0; i < FFT_SIZE; ++i) {
		float w = 0.5f * (1.0f - cosf(2.0f * static_cast<float>(M_PI) * i / (FFT_SIZE - 1)));
		windowed[i] = g_vis.mono_buf[i] * w;
	}
	kiss_fftr(g_vis.fft_cfg, windowed.data(), g_vis.fft_out.data());

	// kiss_fftr magnitudes scale with FFT_SIZE/2 — normalize to [0,1] reference.
	// 0 dB ≈ full-scale single tone with Hann window.
	static constexpr float MAG_SCALE = 2.0f / FFT_SIZE;
	static constexpr float DB_FLOOR  = -70.0f;

	std::array<float, NUM_BARS> frame;
	const float ratio = static_cast<float>(BIN_MAX) / BIN_MIN;
	for (int b = 0; b < NUM_BARS; ++b) {
		int bin_lo = static_cast<int>(BIN_MIN * powf(ratio, static_cast<float>(b)     / NUM_BARS));
		int bin_hi = static_cast<int>(BIN_MIN * powf(ratio, static_cast<float>(b + 1) / NUM_BARS));
		bin_lo = std::clamp(bin_lo, BIN_MIN, BIN_MAX);
		bin_hi = std::clamp(std::max(bin_hi, bin_lo), bin_lo, BIN_MAX);

		float max_mag = 0.0f;
		for (int k = bin_lo; k <= bin_hi; ++k) {
			float re = g_vis.fft_out[k].r;
			float im = g_vis.fft_out[k].i;
			max_mag  = std::max(max_mag, sqrtf(re * re + im * im));
		}

		float db  = 20.0f * log10f(max_mag * MAG_SCALE + 1e-9f);
		float raw = 1.0f - std::clamp(db / DB_FLOOR, 0.0f, 1.0f);

		frame[b] = raw;

		// Smoothed value → bar_heights (fast-rise/slow-fall, for Bars mode)
		if (raw > g_vis.bar_heights[b])
			g_vis.bar_heights[b] = raw;
		else
			g_vis.bar_heights[b] = g_vis.bar_heights[b] * 0.85f + raw * 0.15f;
	}

	g_vis.history.push(frame);
}
