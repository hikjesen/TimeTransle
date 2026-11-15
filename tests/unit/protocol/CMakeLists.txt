add_executable(
  timetransle_protocol_smoke
  protocol_smoke.cpp
)

target_link_libraries(
  timetransle_protocol_smoke
  PRIVATE
    TimeTransle::Protocol
)

timetransle_set_project_warnings(timetransle_protocol_smoke)

add_test(
  NAME protocol.smoke
  COMMAND timetransle_protocol_smoke
)
set_tests_properties(protocol.smoke PROPERTIES TIMEOUT 10)

