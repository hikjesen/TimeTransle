if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  return()
endif()

if(NOT DEFINED TIMETRANSLE_WEBVIEW2_ROOT)
  message(FATAL_ERROR "The x64 Controller requires the pinned WebView2 SDK.")
endif()

set(
  _timetransle_webview2_include
  "${TIMETRANSLE_WEBVIEW2_ROOT}/build/native/include"
)
set(
  _timetransle_webview2_loader
  "${TIMETRANSLE_WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib"
)
if(NOT EXISTS "${_timetransle_webview2_loader}")
  message(FATAL_ERROR "The pinned x64 WebView2 loader library was not found.")
endif()

add_library(timetransle_webview2_loader STATIC IMPORTED)
set_target_properties(
  timetransle_webview2_loader
  PROPERTIES
    IMPORTED_LOCATION "${_timetransle_webview2_loader}"
    INTERFACE_INCLUDE_DIRECTORIES "${_timetransle_webview2_include}"
)

add_executable(
  timetransle_controller
  WIN32
    resources/timetransle.rc
    src/application.cpp
    src/main.cpp
    src/process_icon.cpp
)
set_target_properties(
  timetransle_controller
  PROPERTIES
    OUTPUT_NAME "TimeTransleController"
)

target_link_libraries(
  timetransle_controller
  PRIVATE
    TimeTransle::ControllerCore
    timetransle_webview2_loader
    advapi32
    comdlg32
    gdiplus
    ole32
    shell32
    user32
    version
)
target_include_directories(
  timetransle_controller
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

timetransle_set_project_warnings(timetransle_controller)

set(_timetransle_ui_dist "${PROJECT_SOURCE_DIR}/ui/dist")
if(EXISTS "${_timetransle_ui_dist}/index.html")
  add_custom_command(
    TARGET timetransle_controller
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E make_directory
      "$<TARGET_FILE_DIR:timetransle_controller>/resources/ui"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_directory
      "${_timetransle_ui_dist}"
      "$<TARGET_FILE_DIR:timetransle_controller>/resources/ui"
    VERBATIM
  )
else()
  message(
    WARNING
    "ui/dist is missing; the Controller will build but report a native "
    "missing-resources error until 'npm run build' is run in ui/."
  )
endif()
