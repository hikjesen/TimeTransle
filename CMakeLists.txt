cmake_minimum_required(VERSION 3.28)

project(
  TimeTransle
  VERSION 0.1.5
  DESCRIPTION "Safe local time virtualization for user-selected Windows desktop applications"
  LANGUAGES C CXX
)

if(NOT WIN32)
  message(FATAL_ERROR "TimeTransle V1 supports Windows only.")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(
  TIMETRANSLE_FETCH_DEPENDENCIES
  "Fetch the reviewed, revision-pinned third-party dependencies"
  OFF
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(BuildInfo)
include(CompilerWarnings)
include(Dependencies)

timetransle_fetch_dependencies()

add_subdirectory(src/common)
add_subdirectory(src/protocol)
add_subdirectory(src/virtual_clock)
add_subdirectory(src/ipc)
add_subdirectory(src/safety)
add_subdirectory(src/controller_core)
add_subdirectory(src/hook_core)
add_subdirectory(src/checkpoint)
add_subdirectory(src/hook)
add_subdirectory(src/injector)
add_subdirectory(src/controller)
add_subdirectory(src/test_app)

if(BUILD_TESTING)
  add_subdirectory(tests/unit/common)
  add_subdirectory(tests/unit/protocol)
  add_subdirectory(tests/unit/virtual_clock)
  add_subdirectory(tests/unit/hook_core)
  add_subdirectory(tests/unit/ipc)
  add_subdirectory(tests/unit/safety)
  add_subdirectory(tests/unit/controller_core)
endif()
