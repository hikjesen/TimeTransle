if(NOT TARGET nlohmann_json::nlohmann_json)
  message(FATAL_ERROR "Controller core requires the pinned nlohmann/json dependency.")
endif()

add_library(
  timetransle_controller_core
  STATIC
    src/controller_session.cpp
    src/hook_session.cpp
    src/web_message.cpp
)
add_library(TimeTransle::ControllerCore ALIAS timetransle_controller_core)

target_link_libraries(
  timetransle_controller_core
  PUBLIC
    TimeTransle::Ipc
    TimeTransle::Safety
    nlohmann_json::nlohmann_json
  PRIVATE
    advapi32
    bcrypt
)
target_include_directories(
  timetransle_controller_core
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

timetransle_set_project_warnings(timetransle_controller_core)
