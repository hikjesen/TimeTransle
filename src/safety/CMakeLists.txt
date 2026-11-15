add_library(
  timetransle_safety
  STATIC
    src/policy.cpp
    src/windows_inspector.cpp
)
add_library(TimeTransle::Safety ALIAS timetransle_safety)

target_link_libraries(
  timetransle_safety
  PUBLIC
    TimeTransle::Protocol
  PRIVATE
    advapi32
)
target_include_directories(
  timetransle_safety
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

timetransle_set_project_warnings(timetransle_safety)
