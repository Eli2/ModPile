# SPDX-License-Identifier: GPL-3.0-or-later

project(imgui)

set(imgui_dir ${CMAKE_CURRENT_SOURCE_DIR})
set(imgui_src
	${imgui_dir}/imconfig.h
	${imgui_dir}/imgui.cpp
	${imgui_dir}/imgui_demo.cpp
	${imgui_dir}/imgui_draw.cpp
	${imgui_dir}/imgui.h
	${imgui_dir}/imgui_internal.h
	${imgui_dir}/imgui_tables.cpp
	${imgui_dir}/imgui_widgets.cpp
	${imgui_dir}/imstb_rectpack.h
	${imgui_dir}/imstb_textedit.h
	${imgui_dir}/imstb_truetype.h
	${imgui_dir}/misc/cpp/imgui_stdlib.cpp
	${imgui_dir}/misc/cpp/imgui_stdlib.h
	${imgui_dir}/backends/imgui_impl_sdl3.h
	${imgui_dir}/backends/imgui_impl_sdl3.cpp
	${imgui_dir}/backends/imgui_impl_opengl3.cpp
	${imgui_dir}/backends/imgui_impl_opengl3.h
	${imgui_dir}/backends/imgui_impl_opengl3_loader.h
)

add_library(imgui STATIC ${imgui_src})
add_library(imgui::imgui ALIAS imgui)

target_include_directories(imgui PUBLIC ${imgui_dir} ${imgui_dir}/backends)

target_link_libraries(imgui PUBLIC
	SDL3::SDL3
)
