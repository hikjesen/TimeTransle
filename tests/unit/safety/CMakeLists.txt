add_executable(
  timetransle_safety_tests
  safety_tests.cpp
)

target_link_libraries(
  timetransle_safety_tests
  PRIVATE
    TimeTransle::Safety
)

timetransle_set_project_warnings(timetransle_safety_tests)

add_test(
  NAME safety.unit
  COMMAND timetransle_safety_tests
)
set_tests_properties(safety.unit PROPERTIES TIMEOUT 10)
