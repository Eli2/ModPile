// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2

#include "visualizer_bars.h"
#include "visualizer_common.h"

#include <array>
#include <algorithm>

// ─── Shaders ─────────────────────────────────────────────────────────────────

static const char* VERT_SRC = R"glsl(
#version 150 core
in vec2 aPos;
out float vY;
void main() {
	gl_Position = vec4(aPos, 0.0, 1.0);
	vY = aPos.y * 0.5 + 0.5;
}
)glsl";

static const char* FRAG_SRC = R"glsl(
#version 150 core
in float vY;
out vec4 FragColor;
void main() {
	vec3 col = mix(vec3(0.0, 0.15, 0.6), vec3(0.0, 0.9, 1.0), vY);
	FragColor = vec4(col, 1.0);
}
)glsl";

// ─── State ───────────────────────────────────────────────────────────────────

static struct BarsState {
	GLuint shader_prog = 0;
	GLuint vao = 0, vbo = 0;
} s;

// ─── Public API ──────────────────────────────────────────────────────────────

void bars_init() {
	s.shader_prog = link_program(VERT_SRC, FRAG_SRC);

	glGenVertexArrays(1, &s.vao);
	glGenBuffers(1, &s.vbo);
	glBindVertexArray(s.vao);
	glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
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

void bars_render() {
	static std::array<float, NUM_BARS * 6 * 2> verts;

	const float bar_w = 2.0f / NUM_BARS;
	const float gap   = bar_w * 0.08f;

	for (int i = 0; i < NUM_BARS; ++i) {
		float x0 = -1.0f + i * bar_w + gap;
		float x1 = -1.0f + (i + 1) * bar_w - gap;
		float y0 = -1.0f;
		float y1 = -1.0f + g_vis.bar_heights[i] * 2.0f;
		if (y1 < y0) y1 = y0;

		float* v = verts.data() + i * 12;
		v[0]  = x0; v[1]  = y0;
		v[2]  = x1; v[3]  = y0;
		v[4]  = x1; v[5]  = y1;
		v[6]  = x0; v[7]  = y0;
		v[8]  = x1; v[9]  = y1;
		v[10] = x0; v[11] = y1;
	}

	glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
	             verts.data(), GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glUseProgram(s.shader_prog);
	glBindVertexArray(s.vao);
	glDrawArrays(GL_TRIANGLES, 0, NUM_BARS * 6);
	glBindVertexArray(0);
	glUseProgram(0);
}

void bars_quit() {
	if (s.vao)         glDeleteVertexArrays(1, &s.vao);
	if (s.vbo)         glDeleteBuffers(1, &s.vbo);
	if (s.shader_prog) glDeleteProgram(s.shader_prog);
	s = {};
}
