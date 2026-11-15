add_executable(
  timetransle_injector
  src/main.cpp
)

target_link_libraries(
  timetransle_injector
  PRIVATE
    TimeTransle::Ipc
    TimeTransle::Safety
)
target_include_directories(
  timetransle_injector
  PRIVATE
    "${PROJECT_SOURCE_DIR}/src/hook/include"
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_timetransle_injector_output_name "TimeTransleInjector64")
else()
  set(_timetransle_injector_output_name "TimeTransleInjector32")
endif()

set_target_properties(
  timetransle_injector
  PROPERTIES
    OUTPUT_NAME "${_timetransle_injector_output_name}"
)

timetransle_set_project_warnings(timetransle_injector)
