add_subdirectory(glad)
add_subdirectory(imgui)

set(ENABLE_TESTS OFF CACHE BOOL "Build libebur128 tests" FORCE)
add_subdirectory(libebur128 EXCLUDE_FROM_ALL)
target_include_directories(ebur128 PUBLIC libebur128/ebur128)
