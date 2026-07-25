# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2026 Eli2

add_executable(ModPileTomlTest
	test_ordered_map.cpp
	test_toml.cpp
)
target_link_libraries(ModPileTomlTest PRIVATE
	ModPile::Toml
	Catch2::Catch2WithMain
)

catch_discover_tests(ModPileTomlTest)

add_executable(ModPileTomlBenchmark
	benchmark_toml.cpp
)
target_link_libraries(ModPileTomlBenchmark PRIVATE
	ModPile::Toml
	Catch2::Catch2WithMain
)

add_executable(ModPileTomlTestDecoder
	toml_test_decoder.cpp
)
target_link_libraries(ModPileTomlTestDecoder PRIVATE
	ModPile::Toml
)

add_executable(ModPileTomlTestEncoder
	toml_test_encoder.cpp
)
target_link_libraries(ModPileTomlTestEncoder PRIVATE
	ModPile::Toml
)
add_test(
	NAME ModPileTomlTestDecoderSmoke
	COMMAND
		"${CMAKE_COMMAND}"
		"-DDECODER=$<TARGET_FILE:ModPileTomlTestDecoder>"
		"-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/decoder_smoke.toml"
		"-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/decoder_smoke.json"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/decoder_smoke.cmake"
)
add_test(
	NAME ModPileTomlTestEncoderSmoke
	COMMAND
		"${CMAKE_COMMAND}"
		"-DENCODER=$<TARGET_FILE:ModPileTomlTestEncoder>"
		"-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/decoder_smoke.json"
		"-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/decoder_smoke.toml"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/encoder_smoke.cmake"
)

set(
	MODPILE_TOML_TEST_EXECUTABLE
	""
	CACHE FILEPATH
	"Path to the official toml-test executable"
)

if(MODPILE_TOML_TEST_EXECUTABLE)
	set(_modpile_toml_test_program "${MODPILE_TOML_TEST_EXECUTABLE}")
else()
	find_program(
		_modpile_toml_test_program
		NAMES toml-test
		HINTS "$ENV{HOME}/go/bin"
	)
endif()

if(_modpile_toml_test_program)
	add_custom_target(ModPileTomlConformance
		COMMAND
			"${_modpile_toml_test_program}"
			test
			-toml=1.0
			"-decoder=$<TARGET_FILE:ModPileTomlTestDecoder>"
			"-encoder=$<TARGET_FILE:ModPileTomlTestEncoder>"
		DEPENDS ModPileTomlTestDecoder ModPileTomlTestEncoder
		USES_TERMINAL
	)
else()
	add_custom_target(ModPileTomlConformance
		COMMAND
			"${CMAKE_COMMAND}" -E echo
			"Install the toml-test binary or set MODPILE_TOML_TEST_EXECUTABLE."
		COMMAND "${CMAKE_COMMAND}" -E false
		DEPENDS ModPileTomlTestDecoder ModPileTomlTestEncoder
		USES_TERMINAL
	)
endif()
