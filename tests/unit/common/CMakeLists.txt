add_executable(
  timetransle_common_tests
  diagnostics_tests.cpp
)

target_link_libraries(
  timetransle_common_tests
  PRIVATE
    TimeTransle::Common
)

timetransle_set_project_warnings(timetransle_common_tests)

add_test(
  NAME common.unit
  COMMAND timetransle_common_tests
)
