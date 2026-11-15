add_library(
  timetransle_hook_core
  STATIC
    src/hook_installer.cpp
)
add_library(TimeTransle::HookCore ALIAS timetransle_hook_core)

target_link_libraries(
  timetransle_hook_core
  PUBLIC
    TimeTransle::Common
)
target_include_directories(
  timetransle_hook_core
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

timetransle_set_project_warnings(timetransle_hook_core)
