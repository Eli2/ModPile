add_library(ordered_map INTERFACE)
add_library(tsl::ordered_map ALIAS ordered_map)

target_include_directories(ordered_map SYSTEM INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
