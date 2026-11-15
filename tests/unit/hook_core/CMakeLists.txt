add_executable(
  timetransle_hook_core_tests
  hook_installer_tests.cpp
)

target_link_libraries(
  timetransle_hook_core_tests
  PRIVATE
    TimeTransle::HookCore
)

timetransle_set_project_warnings(timetransle_hook_core_tests)

add_test(
  NAME hook_core.unit
  COMMAND timetransle_hook_core_tests
)
set_tests_properties(hook_core.unit PROPERTIES TIMEOUT 10)

add_executable(
  timetransle_unity_mono_tests
  unity_mono_adapter_tests.cpp
)
add_library(
  timetransle_fake_mono_runtime
  SHARED
  fake_mono_runtime.cpp
  fake_mono_runtime.def
)
set_target_properties(
  timetransle_fake_mono_runtime
  PROPERTIES
    OUTPUT_NAME TimeTransleFakeMonoRuntime
)
timetransle_set_project_warnings(timetransle_fake_mono_runtime)
target_link_libraries(
  timetransle_unity_mono_tests
  PRIVATE
    TimeTransle::UnityMono
)
add_dependencies(
  timetransle_unity_mono_tests
  timetransle_fake_mono_runtime
)
timetransle_set_project_warnings(timetransle_unity_mono_tests)
add_test(
  NAME unity_mono.unit
  COMMAND
    timetransle_unity_mono_tests
    $<TARGET_FILE:timetransle_fake_mono_runtime>
)
set_tests_properties(unity_mono.unit PROPERTIES TIMEOUT 10)
