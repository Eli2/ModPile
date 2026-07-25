# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2026 Eli2

execute_process(
	COMMAND "${DECODER}"
	INPUT_FILE "${INPUT}"
	OUTPUT_VARIABLE actual
	ERROR_VARIABLE decoder_error
	RESULT_VARIABLE decoder_result
)
if(NOT decoder_result EQUAL 0)
	message(FATAL_ERROR "decoder failed (${decoder_result}): ${decoder_error}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT actual STREQUAL expected)
	message(FATAL_ERROR
		"decoder output does not match the tagged JSON protocol\n"
		"expected: ${expected}\n"
		"actual:   ${actual}"
	)
endif()
