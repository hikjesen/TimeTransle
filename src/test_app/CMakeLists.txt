add_executable(
  timetransle_test_app
  src/main.cpp
)
add_executable(
  timetransle_ordinary_fixture
  src/main.cpp
)
add_executable(
  timetransle_elevated_fixture
  WIN32
    src/elevated_fixture.cpp
)
if(MSVC)
  target_link_options(
    timetransle_elevated_fixture
    PRIVATE
      "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
  )
endif()
timetransle_set_project_warnings(
  timetransle_elevated_fixture
)

foreach(
  _target_fixture
  IN ITEMS
    timetransle_test_app
    timetransle_ordinary_fixture
)
  target_link_libraries(
    "${_target_fixture}"
    PRIVATE
      TimeTransle::Common
      bcrypt
      winmm
  )
  target_include_directories(
    "${_target_fixture}"
    PRIVATE
      "${PROJECT_SOURCE_DIR}/src/hook/include"
  )
  add_dependencies("${_target_fixture}" timetransle_hook)
  timetransle_set_project_warnings("${_target_fixture}")
endforeach()

add_library(
  timetransle_anticheat_fixture
  SHARED
    src/anti_cheat_fixture.cpp
)
set_target_properties(
  timetransle_anticheat_fixture
  PROPERTIES
    OUTPUT_NAME "AntiCheatFixture"
)
timetransle_set_project_warnings(
  timetransle_anticheat_fixture
)

add_executable(
  timetransle_safety_preflight_fixture
  src/safety_preflight_fixture.cpp
)
if(MSVC)
  target_compile_options(
    timetransle_safety_preflight_fixture
    PRIVATE
      /guard:cf
  )
  target_link_options(
    timetransle_safety_preflight_fixture
    PRIVATE
      /guard:cf
  )
endif()
add_dependencies(
  timetransle_safety_preflight_fixture
  timetransle_anticheat_fixture
)
timetransle_set_project_warnings(
  timetransle_safety_preflight_fixture
)

add_executable(
  timetransle_safety_preflight_tests
  src/safety_preflight_tests.cpp
)
target_link_libraries(
  timetransle_safety_preflight_tests
  PRIVATE
    TimeTransle::Protocol
)
add_dependencies(
  timetransle_safety_preflight_tests
  timetransle_hook
  timetransle_injector
  timetransle_anticheat_fixture
  timetransle_safety_preflight_fixture
)
timetransle_set_project_warnings(
  timetransle_safety_preflight_tests
)

add_executable(
  timetransle_controller_stub
  src/ipc_controller_stub.cpp
)
target_link_libraries(
  timetransle_controller_stub
  PRIVATE
    TimeTransle::Ipc
    advapi32
)
timetransle_set_project_warnings(timetransle_controller_stub)

add_executable(
  timetransle_injector_harness
  src/injector_harness.cpp
)
target_link_libraries(
  timetransle_injector_harness
  PRIVATE
    TimeTransle::Protocol
)
add_dependencies(
  timetransle_injector_harness
  timetransle_hook
  timetransle_injector
  timetransle_test_app
  timetransle_ordinary_fixture
)
timetransle_set_project_warnings(timetransle_injector_harness)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  add_executable(
    timetransle_controller_e2e
    src/controller_e2e.cpp
  )
  target_link_libraries(
    timetransle_controller_e2e
    PRIVATE
      TimeTransle::ControllerCore
  )
  add_dependencies(
    timetransle_controller_e2e
    timetransle_hook
    timetransle_injector
    timetransle_test_app
    timetransle_ordinary_fixture
  )
  set_target_properties(
    timetransle_controller_e2e
    PROPERTIES
      OUTPUT_NAME "TimeTransleControllerE2E64"
  )
  timetransle_set_project_warnings(timetransle_controller_e2e)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_timetransle_test_app_output_name "TimeTransleTestApp64")
  set(_timetransle_ordinary_fixture_output_name "OrdinaryProgramFixture64")
  set(_timetransle_elevated_fixture_output_name "ElevatedProgramFixture64")
  set(_timetransle_controller_stub_output_name "TimeTransleControllerStub64")
  set(_timetransle_injector_harness_output_name "TimeTransleInjectorHarness64")
  set(_timetransle_safety_preflight_fixture_output_name "MitigatedProgramFixture64")
  set(_timetransle_safety_preflight_tests_output_name "TimeTransleSafetyPreflightTests64")
else()
  set(_timetransle_test_app_output_name "TimeTransleTestApp32")
  set(_timetransle_ordinary_fixture_output_name "OrdinaryProgramFixture32")
  set(_timetransle_elevated_fixture_output_name "ElevatedProgramFixture32")
  set(_timetransle_controller_stub_output_name "TimeTransleControllerStub32")
  set(_timetransle_injector_harness_output_name "TimeTransleInjectorHarness32")
  set(_timetransle_safety_preflight_fixture_output_name "MitigatedProgramFixture32")
  set(_timetransle_safety_preflight_tests_output_name "TimeTransleSafetyPreflightTests32")
endif()

set_target_properties(
  timetransle_test_app
  PROPERTIES
    OUTPUT_NAME "${_timetransle_test_app_output_name}"
)
set_target_properties(
  timetransle_ordinary_fixture
  PROPERTIES
    OUTPUT_NAME "${_timetransle_ordinary_fixture_output_name}"
)
set_target_properties(
  timetransle_elevated_fixture
  PROPERTIES
    OUTPUT_NAME "${_timetransle_elevated_fixture_output_name}"
)
set_target_properties(
  timetransle_controller_stub
  PROPERTIES
    OUTPUT_NAME "${_timetransle_controller_stub_output_name}"
)
set_target_properties(
  timetransle_injector_harness
  PROPERTIES
    OUTPUT_NAME "${_timetransle_injector_harness_output_name}"
)
set_target_properties(
  timetransle_safety_preflight_fixture
  PROPERTIES
    OUTPUT_NAME "${_timetransle_safety_preflight_fixture_output_name}"
)
set_target_properties(
  timetransle_safety_preflight_tests
  PROPERTIES
    OUTPUT_NAME "${_timetransle_safety_preflight_tests_output_name}"
)

add_test(
  NAME hook.integration
  COMMAND timetransle_test_app --direct "$<TARGET_FILE:timetransle_hook>"
)
set_tests_properties(hook.integration PROPERTIES TIMEOUT 30)

foreach(_ipc_mode IN ITEMS disconnect old-session shutdown)
  add_test(
    NAME "ipc.${_ipc_mode}.integration"
    COMMAND
      timetransle_test_app
      --ipc
      "${_ipc_mode}"
      "$<TARGET_FILE:timetransle_hook>"
      "$<TARGET_FILE:timetransle_controller_stub>"
  )
  set_tests_properties("ipc.${_ipc_mode}.integration" PROPERTIES TIMEOUT 30)
endforeach()

add_test(
  NAME injector.integration
  COMMAND
    timetransle_injector_harness
    "$<TARGET_FILE:timetransle_injector>"
    "$<TARGET_FILE:timetransle_test_app>"
    "$<TARGET_FILE_NAME:timetransle_hook>"
)
set_tests_properties(injector.integration PROPERTIES TIMEOUT 30)

add_test(
  NAME safety.preflight.minimum-gates
  COMMAND
    timetransle_safety_preflight_tests
    "$<TARGET_FILE:timetransle_injector>"
    "$<TARGET_FILE:timetransle_safety_preflight_fixture>"
    "$<TARGET_FILE:timetransle_anticheat_fixture>"
)
set_tests_properties(
  safety.preflight.minimum-gates
  PROPERTIES TIMEOUT 30
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  add_test(
    NAME controller.e2e.ordinary.x64
    COMMAND
      timetransle_controller_e2e
      "$<TARGET_FILE:timetransle_ordinary_fixture>"
      x64
      "$<TARGET_FILE_NAME:timetransle_hook>"
      ordinary
  )
  set_tests_properties(
    controller.e2e.ordinary.x64
    PROPERTIES TIMEOUT 20
  )
  add_test(
    NAME controller.e2e.x64
    COMMAND
      timetransle_controller_e2e
      "$<TARGET_FILE:timetransle_test_app>"
      x64
      "$<TARGET_FILE_NAME:timetransle_hook>"
  )
  set_tests_properties(controller.e2e.x64 PROPERTIES TIMEOUT 45)
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  add_executable(timetransle_checkpoint_fixture WIN32 src/checkpoint_fixture.cpp)
  set_target_properties(timetransle_checkpoint_fixture PROPERTIES OUTPUT_NAME "TimeTransleCheckpointFixture32")
  target_link_libraries(timetransle_checkpoint_fixture PRIVATE d3d9 dsound dxguid user32 gdi32)
  timetransle_set_project_warnings(timetransle_checkpoint_fixture)
endif()

