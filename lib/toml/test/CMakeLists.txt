# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2026 Eli2

add_executable(ModPileTomlTest
	test_toml.cpp
)
target_link_libraries(ModPileTomlTest PRIVATE
	ModPile::Toml
	Catch2::Catch2WithMain
)

catch_discover_tests(ModPileTomlTest)
