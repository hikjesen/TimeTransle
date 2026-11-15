add_executable(
  timetransle_virtual_clock_tests
  virtual_clock_tests.cpp
)

target_link_libraries(
  timetransle_virtual_clock_tests
  PRIVATE
    TimeTransle::VirtualClock
)

timetransle_set_project_warnings(timetransle_virtual_clock_tests)

add_test(
  NAME virtual_clock.unit
  COMMAND timetransle_virtual_clock_tests
)
set_tests_properties(virtual_clock.unit PROPERTIES TIMEOUT 30)
