// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2

#include "visualizer_3d_spectrogram.h"
#include "visualizer_common.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

// ─── Shaders ─────────────────────────────────────────────────────────────────

static const char* VERT_SRC = R"glsl(
#version 150 core
uniform mat4 uMVP;
uniform float uMaxDepth;
in vec3 aPos;
in float aMag;
out float vMag;
out float vFog;
void main() {
	gl_Position = uMVP * vec4(aPos, 1.0);
	vMag = aMag;
	vFog = clamp(-aPos.z / uMaxDepth, 0.0, 1.0);
}
)glsl";

// Thermal colour map: dark-blue → cyan → yellow → white, faded to bg by depth.
static const char* FRAG_SRC = R"glsl(
#version 150 core
in float vMag;
in float vFog;
out vec4 FragColor;

vec3 thermal(float t) {
	if (t < 0.25) return mix(vec3(0.0,  0.0,  0.15), vec3(0.0,  0.0,  0.9),  t * 4.0);
	if (t < 0.5)  return mix(vec3(0.0,  0.0,  0.9),  vec3(0.0,  0.9,  1.0),  (t - 0.25) * 4.0);
	if (t < 0.75) return mix(vec3(0.0,  0.9,  1.0),  vec3(1.0,  1.0,  0.0),  (t - 0.5)  * 4.0);
	              return mix(vec3(1.0,  1.0,  0.0),  vec3(1.0,  1.0,  1.0),  (t - 0.75) * 4.0);
}

void main() {
	vec3 col = thermal(vMag);
	col = mix(col, vec3(0.04, 0.04, 0.10), vFog * vFog);
	FragColor = vec4(col, 1.0);
}
)glsl";

// ─── EBO (static, built once) ────────────────────────────────────────────────

static constexpr int EBO_COUNT = (HISTORY_SIZE - 1) * (NUM_BARS - 1) * 6;
// File-scope to avoid large stack frame during init.
static std::array<uint32_t, EBO_COUNT> s_ebo_indices;

// ─── State ───────────────────────────────────────────────────────────────────

static struct Spec3DState {
	GLuint shader_prog = 0;
	GLuint vao = 0, vbo = 0, ebo = 0;
} s;

// ─── Public API ──────────────────────────────────────────────────────────────

void spectrogram3d_init() {
	s.shader_prog = link_program(VERT_SRC, FRAG_SRC);

	glGenVertexArrays(1, &s.vao);
	glGenBuffers(1, &s.vbo);
	glGenBuffers(1, &s.ebo);

	glBindVertexArray(s.vao);
	glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
	// Pre-allocate VBO (content updated each frame via GL_STREAM_DRAW)
	glBufferData(GL_ARRAY_BUFFER,
	             HISTORY_SIZE * NUM_BARS * 4 * static_cast<GLsizeiptr>(sizeof(float)),
	             nullptr, GL_STREAM_DRAW);
	{
		GLint aPos = glGetAttribLocation(s.shader_prog, "aPos");
		GLint aMag = glGetAttribLocation(s.shader_prog, "aMag");
		if (aPos >= 0) {
			glEnableVertexAttribArray(static_cast<GLuint>(aPos));
			glVertexAttribPointer(static_cast<GLuint>(aPos), 3, GL_FLOAT, GL_FALSE,
			                      4 * sizeof(float), reinterpret_cast<void*>(0));
		}
		if (aMag >= 0) {
			glEnableVertexAttribArray(static_cast<GLuint>(aMag));
			glVertexAttribPointer(static_cast<GLuint>(aMag), 1, GL_FLOAT, GL_FALSE,
			                      4 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
		}
	}

	// Build static EBO: two CCW triangles per quad
	for (int row = 0; row < HISTORY_SIZE - 1; ++row) {
		for (int col = 0; col < NUM_BARS - 1; ++col) {
			int      i = (row * (NUM_BARS - 1) + col) * 6;
			uint32_t a = static_cast<uint32_t>(row * NUM_BARS + col);
			uint32_t b = a + 1;
			uint32_t c = a + NUM_BARS;
			uint32_t d = c + 1;
			s_ebo_indices[i + 0] = a; s_ebo_indices[i + 1] = c; s_ebo_indices[i + 2] = b;
			s_ebo_indices[i + 3] = b; s_ebo_indices[i + 4] = c; s_ebo_indices[i + 5] = d;
		}
	}
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
	             static_cast<GLsizeiptr>(s_ebo_indices.size() * sizeof(uint32_t)),
	             s_ebo_indices.data(), GL_STATIC_DRAW);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void spectrogram3d_render(int w, int h) {
	if (g_vis.history.count < 2) return;

	const int active_rows = std::min(g_vis.history.count, HISTORY_SIZE);

	// Build VBO: (x, y, z, mag) per vertex.
	// Row 0 = most recent (z=0), row N = oldest (z = -N * DEPTH_PER_ROW).
	static std::array<float, HISTORY_SIZE * NUM_BARS * 4> verts;
	for (int row = 0; row < active_rows; ++row) {
		int   hist_idx = (g_vis.history.head - 1 - row + HISTORY_SIZE_2D) % HISTORY_SIZE_2D;
		float z        = -static_cast<float>(row) * DEPTH_PER_ROW;
		for (int col = 0; col < NUM_BARS; ++col) {
			float x   = (static_cast<float>(col) / (NUM_BARS - 1) - 0.5f) * GRID_WIDTH;
			float mag = g_vis.history.rows[hist_idx][col];
			float y   = mag * MAX_HEIGHT;
			int   idx = (row * NUM_BARS + col) * 4;
			verts[idx + 0] = x;
			verts[idx + 1] = y;
			verts[idx + 2] = z;
			verts[idx + 3] = mag;
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             active_rows * NUM_BARS * 4 * static_cast<GLsizeiptr>(sizeof(float)),
	             verts.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glm::mat4 proj = glm::perspective(glm::radians(45.0f), static_cast<float>(w) / h,
	                                  0.1f, 200.0f);
	glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 4.0f, 3.0f),
	                             glm::vec3(0.0f, 0.0f, -12.0f),
	                             glm::vec3(0.0f, 1.0f,  0.0f));
	glm::mat4 mvp  = proj * view;

	glUseProgram(s.shader_prog);
	glUniformMatrix4fv(glGetUniformLocation(s.shader_prog, "uMVP"),
	                   1, GL_FALSE, glm::value_ptr(mvp));
	glUniform1f(glGetUniformLocation(s.shader_prog, "uMaxDepth"), MAX_DEPTH);

	glBindVertexArray(s.vao);
	int active_quads = (active_rows - 1) * (NUM_BARS - 1);
	glDrawElements(GL_TRIANGLES, active_quads * 6, GL_UNSIGNED_INT, nullptr);
	glBindVertexArray(0);
	glUseProgram(0);
}

void spectrogram3d_quit() {
	if (s.vao)         glDeleteVertexArrays(1, &s.vao);
	if (s.vbo)         glDeleteBuffers(1, &s.vbo);
	if (s.ebo)         glDeleteBuffers(1, &s.ebo);
	if (s.shader_prog) glDeleteProgram(s.shader_prog);
	s = {};
}
