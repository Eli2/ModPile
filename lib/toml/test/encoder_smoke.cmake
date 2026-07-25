# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2026 Eli2

execute_process(
	COMMAND "${ENCODER}"
	INPUT_FILE "${INPUT}"
	OUTPUT_VARIABLE actual
	ERROR_VARIABLE encoder_error
	RESULT_VARIABLE encoder_result
)
if(NOT encoder_result EQUAL 0)
	message(FATAL_ERROR "encoder failed (${encoder_result}): ${encoder_error}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT actual STREQUAL expected)
	message(FATAL_ERROR
		"encoder output does not match the expected TOML\n"
		"expected: ${expected}\n"
		"actual:   ${actual}"
	)
endif()
