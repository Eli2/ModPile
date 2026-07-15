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
option(MODPILE_USE_SYSTEM_OPENAL     "Use system OpenAL"     ON)
option(MODPILE_USE_SYSTEM_CATCH2     "Use system Catch2"     ON)
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
find_package(CURL REQUIRED)

include(FetchContent)

set(BUILD_SHARED_LIBS OFF)

if(MODPILE_USE_SYSTEM_OPENAL)
	find_package(OpenAL REQUIRED)
else()
	FetchContent_Declare(
		openal-soft
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/kcat/openal-soft
		GIT_TAG        b2c48f7718ef3fcf67921a8b6534c4914e328970 # 1.25.2
	)

	set(ALSOFT_EXAMPLES OFF)
	set(ALSOFT_TESTS    OFF)
	set(ALSOFT_UTILS    OFF)
	set(ALSOFT_INSTALL  OFF)
	FetchContent_MakeAvailable(openal-soft)
endif()

if(MODPILE_USE_SYSTEM_EBUR128)
	pkg_check_modules(ebur128 REQUIRED IMPORTED_TARGET libebur128)
else()
	FetchContent_Declare(
		libebur128
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/jiixyj/libebur128
		GIT_TAG        67b33abe1558160ed76ada1322329b0e9e058b02 # v1.2.6
		PATCH_COMMAND  find <SOURCE_DIR> -name "CMakeLists.txt" -exec sed -i "s/cmake_minimum_required([^)]*)/cmake_minimum_required(VERSION 3.13)/" {} +
	)

	set(ENABLE_TESTS OFF)
	FetchContent_MakeAvailable(libebur128)
	target_include_directories(ebur128 PUBLIC ${libebur128_SOURCE_DIR}/ebur128)
endif()

if(MODPILE_USE_SYSTEM_SDL)
	find_package(SDL3 REQUIRED)
	find_package(SDL3_image REQUIRED)
else()
	FetchContent_Declare(
		SDL3
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/libsdl-org/SDL
		GIT_TAG        f87239e71e42da91ca317a12eefb82cfbf3393eb # 3.4.12
	)
	FetchContent_Declare(
		SDL3_image
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/libsdl-org/SDL_image
		GIT_TAG        bec9134a26c7d0f31b36d6083c25296e04cabff5 # 3.4.4
	)

	set(SDL_SHARED OFF)
	set(SDL_STATIC ON)
	set(SDL_TEST_LIBRARY OFF)
	set(SDL_AUDIO OFF)
	FetchContent_MakeAvailable(SDL3 SDL3_image)
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
	FetchContent_Declare(
		libarchive
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/libarchive/libarchive
		GIT_TAG        7219b0134d771dc4b51bf86b4d01761b87398b1b # 3.8.8
	)

	set(ENABLE_TEST OFF)
	set(ENABLE_CAT OFF)
	set(ENABLE_CPIO OFF)
	set(ENABLE_TAR OFF)
	set(ENABLE_UNZIP OFF)
	FetchContent_MakeAvailable(libarchive)
	add_library(LibArchive::LibArchive ALIAS archive_static)
endif()

if(MODPILE_USE_SYSTEM_LIBXMP)
	find_package(libxmp REQUIRED)
else()
	FetchContent_Declare(
		libxmp
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/libxmp/libxmp
		GIT_TAG        12f76e4bf1dd6765e3bf46cbc1ff35c4a4551671 # 4.7.1
	)
	
	set(BUILD_SHARED OFF)
	set(LIBXMP_DISABLE_DEPACKERS ON)
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
	
	# kissfft's old cmake_minimum_required keeps CMP0077 at OLD, so option()
	# ignores normal variables; cache entries with FORCE are required here.
	set(KISSFFT_DATATYPE float CACHE STRING "" FORCE)
	set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
	set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
	set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
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
	src/db/schema/schema.cpp
	src/db/schema/schema.h
	src/db/schema/V001_Initial_schema.cpp
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
	src/task/vacuum.cpp
	src/task/vacuum.h
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
	src/util/xml_util.h
	src/util/xml_util.cpp
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
	#-Wshadow
	-Wnull-dereference
	-Wimplicit-fallthrough
	-Wformat=2
)

target_compile_features(ModPile PRIVATE
	cxx_std_20
)

if(MODPILE_ENABLE_MPRIS)
	target_compile_definitions(ModPile PRIVATE HAVE_MPRIS)
endif()

if(MODPILE_USE_SYSTEM_KISSFFT)
	target_compile_definitions(ModPile PRIVATE KISSFFT_SYSTEM_HEADERS)
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

if(MODPILE_USE_SYSTEM_CATCH2)
	find_package(Catch2 3 REQUIRED)
else()
	FetchContent_Declare(
		Catch2
		GIT_SHALLOW    ON
		GIT_REPOSITORY https://github.com/catchorg/Catch2
		GIT_TAG        191fa38c9b1596cd2576ab531d4ab4d5e8e05190 # v3.15.2
	)
	FetchContent_MakeAvailable(Catch2)
endif()

add_executable(ModPileTest
	${hash_util_srcs}
	src/util/str_util.h
	src/util/str_util.cpp
	src/util/sqlite_util.h
	src/util/sqlite_util.cpp
	src/util/xml_util.h
	src/util/xml_util.cpp
	src/log.h
	src/log.cpp
	src/db/schema/schema.h
	src/db/schema/schema.cpp
	src/db/schema/V001_Initial_schema.cpp

	test/test_hash_util.cpp
	test/test_str_util.cpp
	test/test_db_migrate.cpp
	test/test_xml_util.cpp
)
target_compile_features(ModPileTest PRIVATE
	cxx_std_20
)
target_link_libraries(ModPileTest PRIVATE
	Catch2::Catch2WithMain
	SQLite::SQLite3
)

include(CTest)
include(Catch)
catch_discover_tests(ModPileTest)
