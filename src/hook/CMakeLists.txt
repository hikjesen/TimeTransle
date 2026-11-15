if(NOT TARGET minhook)
  message(FATAL_ERROR "The Hook target requires the pinned MinHook dependency.")
endif()

add_library(
  timetransle_unity_mono
  STATIC
    src/unity_mono_adapter.cpp
)
add_library(TimeTransle::UnityMono ALIAS timetransle_unity_mono)

target_link_libraries(
  timetransle_unity_mono
  PUBLIC
    TimeTransle::Protocol
)
target_include_directories(
  timetransle_unity_mono
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
timetransle_set_project_warnings(timetransle_unity_mono)

add_library(
  timetransle_hook
  SHARED
    src/dll_main.cpp
    src/hook_runtime.cpp
)

target_link_libraries(
  timetransle_hook
  PRIVATE
    TimeTransle::HookCore
    TimeTransle::Checkpoint
    TimeTransle::Ipc
    TimeTransle::UnityMono
    TimeTransle::VirtualClock
    minhook
    winmm
)
target_include_directories(
  timetransle_hook
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_compile_definitions(
  timetransle_hook
  PRIVATE
    TIMETRANSLE_HOOK_EXPORTS
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_timetransle_hook_output_name "TimeTransleHook64")
else()
  set(_timetransle_hook_output_name "TimeTransleHook32")
  target_sources(
    timetransle_hook
    PRIVATE
      exports_x86.def
  )
endif()

set_target_properties(
  timetransle_hook
  PROPERTIES
    OUTPUT_NAME "${_timetransle_hook_output_name}"
)

timetransle_set_project_warnings(timetransle_hook)
