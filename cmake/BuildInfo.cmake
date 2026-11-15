include_guard(GLOBAL)

function(timetransle_configure_build_info target_name)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TIMETRANSLE_BUILD_ARCHITECTURE "x64")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(TIMETRANSLE_BUILD_ARCHITECTURE "x86")
  else()
    message(FATAL_ERROR "Unsupported pointer size: ${CMAKE_SIZEOF_VOID_P}")
  endif()

  execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE git_result
    OUTPUT_VARIABLE git_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  if(NOT git_result EQUAL 0 OR git_revision STREQUAL "")
    set(git_revision "uncommitted")
  endif()

  set(TIMETRANSLE_BUILD_REVISION "${git_revision}")
  set(
    TIMETRANSLE_BUILD_ID
    "${PROJECT_VERSION}+${TIMETRANSLE_BUILD_REVISION}.${TIMETRANSLE_BUILD_ARCHITECTURE}"
  )

  set(generated_include_dir "${PROJECT_BINARY_DIR}/generated/include")
  file(MAKE_DIRECTORY "${generated_include_dir}/timetransle/common")

  configure_file(
    "${PROJECT_SOURCE_DIR}/src/common/include/timetransle/common/build_info.hpp.in"
    "${generated_include_dir}/timetransle/common/build_info.hpp"
    @ONLY
  )

  target_include_directories(
    "${target_name}"
    INTERFACE
      "$<BUILD_INTERFACE:${generated_include_dir}>"
  )
endfunction()

