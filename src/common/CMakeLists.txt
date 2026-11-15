add_library(
  timetransle_common
  STATIC
    src/diagnostics.cpp
)
add_library(TimeTransle::Common ALIAS timetransle_common)

target_compile_features(timetransle_common PUBLIC cxx_std_20)
target_compile_definitions(
  timetransle_common
  PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)
target_include_directories(
  timetransle_common
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

timetransle_configure_build_info(timetransle_common)
timetransle_set_project_warnings(timetransle_common)
