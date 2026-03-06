# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2025-2026 Eli2

cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

project(
	ModPile
	VERSION 0.0.1
	LANGUAGES C CXX
)

set(MODPILE_SANITIZE none CACHE STRING "Enable Sanitizers")
set_property(CACHE MODPILE_SANITIZE PROPERTY STRINGS none address thread undefined)

option(MODPILE_IPO                   "Enable IPO"            OFF)
option(MODPILE_USE_SYSTEM_LIBARCHIVE "Use system libarchive" ON)
option(MODPILE_USE_SYSTEM_LIBXMP     "Use system libxmp"     ON)
option(MODPILE_USE_SYSTEM_ZSTD       "Use system zstd"       ON)
option(MODPILE_USE_SYSTEM_SDL        "Use system SDL"        ON)
option(MODPILE_USE_SYSTEM_EBUR128    "Use system ebur128"    ON)
option(MODPILE_USE_SYSTEM_KISSFFT    "Use system kissfft"    ON)
option(MODPILE_USE_SYSTEM_GLM        "Use system glm"        ON)
option(MODPILE_ENABLE_MPRIS          "Enable MPRIS D-Bus support (requires libsystemd)" ON)

message(STATUS "=======================================")

if(MODPILE_IPO)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
	if(ipo_supported)
		message(STATUS "IPO / LTO enabled")
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
	else()
		message(STATUS "IPO / LTO not supported: <${ipo_error}>")
	endif()
else()
	message(STATUS "IPO disabled")
endif()

if(MODPILE_SANITIZE STREQUAL "address")
	add_compile_options(-fsanitize=address)
	add_link_options(-fsanitize=address)
	message(STATUS "Asan enabled")
elseif(MODPILE_SANITIZE STREQUAL "thread")
	add_compile_options(-fsanitize=thread)
	add_link_options(-fsanitize=thread)
	message(STATUS "Tsan enabled")
elseif(MODPILE_SANITIZE STREQUAL "undefined")
	add_compile_options(-fsanitize=undefined)
	add_link_options(-fsanitize=undefined)
	message(STATUS "UBsan enabled")
else()
	message(STATUS "Sanitizers disabled")
endif()

message(STATUS "=======================================")



find_package(PkgConfig REQUIRED)
if(MODPILE_ENABLE_MPRIS)
	pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
	message(STATUS "MPRIS support enabled")
else()
	message(STATUS "MPRIS support disabled")
endif()



find_package(SQLite3 REQUIRED)
find_package(OpenAL REQUIRED)
find_package(CURL REQUIRED)

include(FetchContent)

set(BUILD_SHARED_LIBS OFF)

if(MODPILE_USE_SYSTEM_EBUR128)
	pkg_check_modules(ebur128 REQUIRED IMPORTED_TARGET libebur128)
else()
	FetchContent_Declare(
		libebur128
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/jiixyj/libebur128
		GIT_TAG        67b33abe1558160ed76ada1322329b0e9e058b02 # v1.2.6
		PATCH_COMMAND  sed -i "s/cmake_minimum_required([^)]*)/cmake_minimum_required(VERSION 3.5)/" CMakeLists.txt
	)
	FetchContent_MakeAvailable(libebur128)
endif()

if(MODPILE_USE_SYSTEM_SDL)
	find_package(SDL3 REQUIRED)
	find_package(SDL3_image REQUIRED)
	find_package(SDL3_ttf REQUIRED)
else()
	set(SDL_SHARED OFF)
	set(SDL_STATIC ON)
	set(SDL_TEST_LIBRARY OFF)
	set(SDL_AUDIO OFF)
	FetchContent_Declare(
		SDL3
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/libsdl-org/SDL
		GIT_TAG        683181b47cfabd293e3ea409f838915b8297a4fd # 3.4.2
	)
	FetchContent_Declare(
		SDL3_image
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/libsdl-org/SDL_image
		GIT_TAG        ad58ecfc27a1bd09e510ceff8bbbdb1094806476 # 3.4.0
	)
	FetchContent_Declare(
		SDL3_ttf
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf
		GIT_TAG        a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b # 3.2.2
	)
	FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_ttf)
endif()


if(MODPILE_USE_SYSTEM_ZSTD)
	find_package(zstd REQUIRED)
else()
	FetchContent_Declare(
		zstd
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/facebook/zstd
		GIT_TAG        f8745da6ff1ad1e7bab384bd1f9d742439278e99 # v1.5.7
		SOURCE_SUBDIR  build/cmake
	)
	
	set(ZSTD_BUILD_STATIC ON)
	set(ZSTD_BUILD_SHARED OFF)
	set(ZSTD_LEGACY_SUPPORT OFF)
	set(ZSTD_BUILD_PROGRAMS OFF)
	set(ZSTD_BUILD_TESTS OFF)
	FetchContent_MakeAvailable(zstd)
endif()

if(MODPILE_USE_SYSTEM_LIBARCHIVE)
	find_package(LibArchive REQUIRED)
else()
	set(ENABLE_TEST OFF)
	FetchContent_Declare(
		libarchive
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/libarchive/libarchive
		GIT_TAG        dd897a78c662a2c7a003e7ec158cea7909557bee # 3.8.5
	)
	FetchContent_MakeAvailable(libarchive)
	add_library(LibArchive::LibArchive ALIAS archive_static)
endif()

if(MODPILE_USE_SYSTEM_LIBXMP)
	find_package(libxmp REQUIRED)
else()
	set(BUILD_SHARED OFF)
	set(LIBXMP_DISABLE_DEPACKERS ON)
	FetchContent_Declare(
		libxmp
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/libxmp/libxmp
		GIT_TAG        8a4fdf7d09aedc921de537853b59e1d15b534f24 # 4.7.0
	)
	FetchContent_MakeAvailable(libxmp)
endif()

if(MODPILE_USE_SYSTEM_KISSFFT)
	find_package(kissfft REQUIRED COMPONENTS SHARED)
else()
	FetchContent_Declare(
		kissfft
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
		GIT_TAG        7bce4153c6bc8aba2db0e889e576f9d00505cbe1 # 131.2.0
	)
	
	set(KISSFFT_DATATYPE float CACHE STRING "")
	set(KISSFFT_STATIC ON CACHE BOOL "")
	set(KISSFFT_TEST OFF CACHE BOOL "")
	set(KISSFFT_TOOLS OFF CACHE BOOL "")
	FetchContent_MakeAvailable(kissfft)
endif()

if(MODPILE_USE_SYSTEM_GLM)
	find_package(glm REQUIRED)
else()
	FetchContent_Declare(
		glm
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/g-truc/glm.git
		GIT_TAG        8d1fd52e5ab5590e2c81768ace50c72bae28f2ed # 1.0.3
	)
	FetchContent_MakeAvailable(glm)
endif()

add_subdirectory(extern)

# Main

set(hash_util_srcs
	src/util/hash/md5.h
	src/util/hash/md5.cpp
	src/util/hash/sha1.h
	src/util/hash/sha1.cpp
	src/util/hash_util.h
	src/util/hash_util.cpp
)

add_executable(ModPile
	src/task/analyze.cpp
	src/task/analyze.h
	src/task/check.cpp
	src/task/check.h
	src/task/export.cpp
	src/task/export.h
	src/task/import.cpp
	src/task/import.h
	src/task/load.cpp
	src/task/load.h
	src/task/modland.cpp
	src/task/modland.h
	src/task_util.cpp
	src/task_util.h
	src/task.cpp
	src/task.h
	
	${hash_util_srcs}
	src/util/archive_util.h
	src/util/curl_util.h
	src/util/curl_util.cpp
	src/util/defer_util.h
	src/util/sqlite_util.h
	src/util/sqlite_util.cpp
	src/util/str_util.h
	src/util/str_util.cpp
	src/util/thread_util.h
	src/util/thread_util.cpp
	src/util/timer_util.h
	src/util/xmp_util.h
	src/util/xmp_util.cpp
	src/util/toml.h
	src/util/toml.cpp

	src/blocking_queue.h
	src/config.h
	src/config.cpp
	src/db_common.h
	src/db_common.cpp
	src/db.h
	src/db.cpp
	src/global.cpp
	src/global.h
	src/gui/gui.h
	src/gui/gui.cpp
	src/gui/gui_common.h
	src/gui/gui_charts.h
	src/gui/gui_charts.cpp
	src/gui/gui_menu.h
	src/gui/gui_menu.cpp
	src/gui/gui_misc.h
	src/gui/gui_misc.cpp
	src/gui/gui_pile.h
	src/gui/gui_pile.cpp
	src/gui/gui_player.h
	src/gui/gui_player.cpp
	src/gui/gui_playlist.h
	src/gui/gui_playlist.cpp
	src/setup.h
	src/setup.cpp
	src/mpris.h
	src/mpris.cpp
	src/log.h
	src/log.cpp
	src/main.cpp
	src/numblock.h
	src/numblock.cpp
	src/player.h
	src/player.cpp
	src/charts.h
	src/charts.cpp
	src/playlist.h
	src/playlist.cpp
	src/query.h
	src/query.cpp
	src/tray.h
	src/tray.cpp
	src/visualizer/visualizer.h
	src/visualizer/visualizer.cpp
	src/visualizer/visualizer_common.h
	src/visualizer/visualizer_utils.cpp
	src/visualizer/visualizer_bars.h
	src/visualizer/visualizer_bars.cpp
	src/visualizer/visualizer_2d_spectrogram.h
	src/visualizer/visualizer_2d_spectrogram.cpp
	src/visualizer/visualizer_3d_spectrogram.h
	src/visualizer/visualizer_3d_spectrogram.cpp
	src/util/ring_buffer.h
)

target_include_directories(ModPile PRIVATE
	src/
	src/gui/
	src/visualizer/
)

target_compile_options(ModPile PRIVATE
	-Wall
	-Wextra
	-Wpedantic
	-Wno-unused-parameter
)

target_compile_features(ModPile PRIVATE
	#c_std_17
	cxx_std_20
)

if(MODPILE_ENABLE_MPRIS)
	target_compile_definitions(ModPile PRIVATE HAVE_MPRIS)
endif()

set_target_properties(ModPile PROPERTIES
	LINK_FLAGS_RELEASE -s
)

target_link_libraries(ModPile PRIVATE
	LibArchive::LibArchive
	SQLite::SQLite3
	$<$<BOOL:${MODPILE_ENABLE_MPRIS}>:systemd>
	SDL3::SDL3
	SDL3_image::SDL3_image
	SDL3_ttf::SDL3_ttf
	imgui::imgui
	OpenAL::OpenAL
	ebur128
	CURL::libcurl
	glad::glad
	kissfft::kissfft-float
	glm::glm
)
if(MODPILE_USE_SYSTEM_ZSTD)
	target_link_libraries(ModPile PRIVATE zstd::libzstd_shared)
else()
	target_link_libraries(ModPile PRIVATE libzstd_static)
endif()

if(MODPILE_USE_SYSTEM_LIBXMP)
	target_link_libraries(ModPile PRIVATE libxmp::xmp_shared)
else()
	target_link_libraries(ModPile PRIVATE libxmp::xmp_static)
endif()



# =============================================================================

find_package(Catch2 3 REQUIRED)

add_executable(ModPileTest
	${hash_util_srcs}
	src/util/str_util.h
	src/util/str_util.cpp
	
	test/test_hash_util.cpp
	test/test_str_util.cpp
)
target_compile_features(ModPileTest PRIVATE
	#c_std_17
	cxx_std_20
)
target_link_libraries(ModPileTest PRIVATE
	Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(ModPileTest)
