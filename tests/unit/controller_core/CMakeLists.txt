add_executable(
  timetransle_controller_core_tests
  controller_core_tests.cpp
)

target_link_libraries(
  timetransle_controller_core_tests
  PRIVATE
    TimeTransle::ControllerCore
)

timetransle_set_project_warnings(timetransle_controller_core_tests)

add_test(
  NAME controller_core.unit
  COMMAND timetransle_controller_core_tests
)
set_tests_properties(controller_core.unit PROPERTIES TIMEOUT 10)
