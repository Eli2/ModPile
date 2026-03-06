// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2

#include "visualizer_bars.h"
#include "visualizer_common.h"

// ─── Shaders ─────────────────────────────────────────────────────────────────

static const char* VERT_SRC = R"glsl(
#version 150 core
in vec2 aPos;
void main() {
	gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

// All layout logic lives here — CPU just passes raw heights and resolution.
// num_bars  = min(w / MIN_SLOT_PX, NUM_BARS)
// slot_w    = w / num_bars
// x_offset  = centered when below max bars, flush-left at max bars
// Bar combining (max of group) runs per-fragment for the relevant data range.
static const char* FRAG_SRC = R"glsl(
#version 150 core
uniform ivec2 uResolution;
uniform float uHeights[256];
out vec4 FragColor;

const int NUM_BARS    = 256;
const int MIN_SLOT_PX = 4;

void main() {
	int w        = uResolution.x;
	int num_bars = min(w / MIN_SLOT_PX, NUM_BARS);
	if (num_bars < 1) discard;

	int slot_w   = w / num_bars;
	int x_offset = (num_bars < NUM_BARS) ? (w - slot_w * num_bars) / 2 : 0;

	int px = int(gl_FragCoord.x) - x_offset;
	if (px < 0) discard;

	int bar_idx_raw = px / slot_w;
	int bar_idx     = clamp(bar_idx_raw, 0, num_bars - 1);
	int local       = px - bar_idx * slot_w;

	// Combine the data bars that map to this display bar (max of group).
	int lo = bar_idx * NUM_BARS / num_bars;
	int hi = (bar_idx + 1) * NUM_BARS / num_bars;
	float height = 0.0;
	for (int j = lo; j < hi; ++j)
		height = max(height, uHeights[j]);

	float y = gl_FragCoord.y / float(uResolution.y);
	if (y > height) discard;

	// Gap overlay — integer grid, no moiré.
	// Skipped for clamped right-edge pixels so the last bar fills to the edge.
	if (bar_idx_raw < num_bars) {
		int gap = (slot_w >= 3) ? max(slot_w / 8, 1) : 0;
		if (local < gap || local >= slot_w - gap) {
			FragColor = vec4(0.04, 0.04, 0.10, 1.0);
			return;
		}
	}

	FragColor = vec4(mix(vec3(0.0, 0.15, 0.6), vec3(0.0, 0.9, 1.0), y), 1.0);
}
)glsl";

// ─── State ───────────────────────────────────────────────────────────────────

static struct BarsState {
	GLuint shader_prog    = 0;
	GLuint vao = 0, vbo   = 0;
	GLint  loc_resolution = -1;
	GLint  loc_heights    = -1;
} s;

static constexpr float QUAD[] = {
	-1.f, -1.f,   1.f, -1.f,   1.f,  1.f,
	-1.f, -1.f,   1.f,  1.f,  -1.f,  1.f,
};

// ─── Public API ──────────────────────────────────────────────────────────────

void bars_init() {
	s.shader_prog    = link_program(VERT_SRC, FRAG_SRC);
	s.loc_resolution = glGetUniformLocation(s.shader_prog, "uResolution");
	s.loc_heights    = glGetUniformLocation(s.shader_prog, "uHeights");

	glGenVertexArrays(1, &s.vao);
	glGenBuffers(1, &s.vbo);
	glBindVertexArray(s.vao);
	glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);
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
}

void bars_render(int w, int h) {
	if (w / 4 < 1) return;

	glUseProgram(s.shader_prog);
	glUniform2i(s.loc_resolution, w, h);
	glUniform1fv(s.loc_heights, NUM_BARS, g_vis.bar_heights.data());

	glBindVertexArray(s.vao);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glUseProgram(0);
}

void bars_quit() {
	if (s.vao)         glDeleteVertexArrays(1, &s.vao);
	if (s.vbo)         glDeleteBuffers(1, &s.vbo);
	if (s.shader_prog) glDeleteProgram(s.shader_prog);
	s = {};
}
