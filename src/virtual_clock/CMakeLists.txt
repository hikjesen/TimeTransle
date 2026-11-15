add_library(
  timetransle_virtual_clock
  STATIC
    src/virtual_clock.cpp
)
add_library(TimeTransle::VirtualClock ALIAS timetransle_virtual_clock)

target_link_libraries(
  timetransle_virtual_clock
  PUBLIC
    TimeTransle::Common
)
target_include_directories(
  timetransle_virtual_clock
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

timetransle_set_project_warnings(timetransle_virtual_clock)
