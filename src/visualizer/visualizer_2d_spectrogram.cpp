// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2

#include "visualizer_2d_spectrogram.h"
#include "visualizer_common.h"

#include <algorithm>
#include <array>

// ─── Shaders ─────────────────────────────────────────────────────────────────

static const char* VERT_SRC = R"glsl(
#version 150 core
in vec2 aPos;
out vec2 vUV;
void main() {
	gl_Position = vec4(aPos, 0.0, 1.0);
	vUV = aPos * 0.5 + 0.5;
}
)glsl";

// Same thermal colour map as the 3D spectrogram.
static const char* FRAG_SRC = R"glsl(
#version 150 core
uniform sampler2D uTex;
in vec2 vUV;
out vec4 FragColor;

vec3 thermal(float t) {
	if (t < 0.25) return mix(vec3(0.0, 0.0, 0.15), vec3(0.0, 0.0, 0.9),  t          * 4.0);
	if (t < 0.5)  return mix(vec3(0.0, 0.0, 0.9),  vec3(0.0, 0.9, 1.0), (t - 0.25) * 4.0);
	if (t < 0.75) return mix(vec3(0.0, 0.9, 1.0),  vec3(1.0, 1.0, 0.0), (t - 0.5)  * 4.0);
	              return mix(vec3(1.0, 1.0, 0.0),  vec3(1.0, 1.0, 1.0), (t - 0.75) * 4.0);
}

void main() {
	float mag = texture(uTex, vUV).r;
	FragColor = vec4(thermal(mag), 1.0);
}
)glsl";

// ─── State ───────────────────────────────────────────────────────────────────

static struct Spec2DState {
	GLuint shader_prog = 0;
	GLuint vao = 0, vbo = 0;
	GLuint tex = 0;
} s;

// ─── Public API ──────────────────────────────────────────────────────────────

void spectrogram2d_init() {
	s.shader_prog = link_program(VERT_SRC, FRAG_SRC);

	// Fullscreen quad: two CCW triangles covering NDC [-1,1]^2.
	static constexpr float kQuad[12] = {
		-1.f,-1.f,  1.f,-1.f,  1.f, 1.f,
		-1.f,-1.f,  1.f, 1.f, -1.f, 1.f,
	};
	glGenVertexArrays(1, &s.vao);
	glGenBuffers(1, &s.vbo);
	glBindVertexArray(s.vao);
	glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
	{
		GLint loc = glGetAttribLocation(s.shader_prog, "aPos");
		if (loc >= 0) {
			glEnableVertexAttribArray(static_cast<GLuint>(loc));
			glVertexAttribPointer(static_cast<GLuint>(loc), 2, GL_FLOAT, GL_FALSE,
			                      2 * sizeof(float), nullptr);
		}
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	// Waterfall texture: NUM_BARS wide × HISTORY_SIZE_2D tall, single-channel float.
	// Row 0 (GL bottom) maps to screen-top via the shared ImGui UV flip,
	// so row 0 = oldest, row HISTORY_SIZE_2D-1 = most recent.
	glGenTextures(1, &s.tex);
	glBindTexture(GL_TEXTURE_2D, s.tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, NUM_BARS, HISTORY_SIZE_2D, 0,
	             GL_RED, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void spectrogram2d_render() {
	// Linearize the circular history_2d buffer into texture rows.
	// head is the oldest slot, so row r = (head + r) % HISTORY_SIZE_2D
	// gives row 0 = oldest (screen-top) and row HISTORY_SIZE_2D-1 = newest (screen-bottom).
	static std::array<float, HISTORY_SIZE_2D * NUM_BARS> pixels;
	for (int r = 0; r < HISTORY_SIZE_2D; ++r) {
		int hist_idx = (g_vis.history.head + r) % HISTORY_SIZE_2D;
		std::copy(g_vis.history.rows[hist_idx].begin(), g_vis.history.rows[hist_idx].end(),
		          pixels.begin() + r * NUM_BARS);
	}

	glBindTexture(GL_TEXTURE_2D, s.tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, NUM_BARS, HISTORY_SIZE_2D,
	                GL_RED, GL_FLOAT, pixels.data());

	glUseProgram(s.shader_prog);
	glUniform1i(glGetUniformLocation(s.shader_prog, "uTex"), 0);
	glBindVertexArray(s.vao);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glUseProgram(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void spectrogram2d_quit() {
	if (s.tex)         glDeleteTextures(1, &s.tex);
	if (s.vao)         glDeleteVertexArrays(1, &s.vao);
	if (s.vbo)         glDeleteBuffers(1, &s.vbo);
	if (s.shader_prog) glDeleteProgram(s.shader_prog);
	s = {};
}
