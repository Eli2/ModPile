# SPDX-License-Identifier: MIT

project(glad)

add_library(glad OBJECT
	include/KHR/khrplatform.h
	include/glad/glad.h
	src/glad.c
)
add_library(glad::glad ALIAS glad)

target_include_directories(glad PUBLIC
	include
)
