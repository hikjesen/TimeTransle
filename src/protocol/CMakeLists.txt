add_library(
  timetransle_protocol
  STATIC
    src/error_code.cpp
)
add_library(TimeTransle::Protocol ALIAS timetransle_protocol)

target_link_libraries(
  timetransle_protocol
  PUBLIC
    TimeTransle::Common
)
target_include_directories(
  timetransle_protocol
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

timetransle_set_project_warnings(timetransle_protocol)

