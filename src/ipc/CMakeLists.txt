add_library(
  timetransle_ipc
  STATIC
    src/pipe_io.cpp
    src/protocol.cpp
)
add_library(TimeTransle::Ipc ALIAS timetransle_ipc)

target_link_libraries(
  timetransle_ipc
  PUBLIC
    TimeTransle::Protocol
)
target_include_directories(
  timetransle_ipc
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

timetransle_set_project_warnings(timetransle_ipc)
