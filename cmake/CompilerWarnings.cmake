include_guard(GLOBAL)

function(timetransle_set_project_warnings target_name)
  if(MSVC)
    target_compile_options(
      "${target_name}"
      PRIVATE
        /W4
        /WX
        /permissive-
        /utf-8
        /Zc:__cplusplus
        /EHsc
    )
  else()
    target_compile_options(
      "${target_name}"
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
  endif()
endfunction()

