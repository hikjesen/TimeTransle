add_executable(
  timetransle_ipc_tests
  ipc_tests.cpp
)

target_link_libraries(
  timetransle_ipc_tests
  PRIVATE
    TimeTransle::Ipc
)

timetransle_set_project_warnings(timetransle_ipc_tests)

add_test(
  NAME ipc.unit
  COMMAND timetransle_ipc_tests
)
set_tests_properties(ipc.unit PROPERTIES TIMEOUT 15)
add_executable(timetransle_checkpoint_protocol_tests checkpoint_protocol_tests.cpp)
target_link_libraries(timetransle_checkpoint_protocol_tests PRIVATE TimeTransle::Ipc)
timetransle_set_project_warnings(timetransle_checkpoint_protocol_tests)
add_test(NAME checkpoint.protocol COMMAND timetransle_checkpoint_protocol_tests)

