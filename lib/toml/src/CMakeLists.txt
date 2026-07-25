# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2026 Eli2

add_library(ModPileToml STATIC
	toml.cpp
	toml.h
)
add_library(ModPile::Toml ALIAS ModPileToml)

target_compile_features(ModPileToml PUBLIC
	cxx_std_20
)
target_include_directories(ModPileToml PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
)
