include_guard(GLOBAL)

set(
  TIMETRANSLE_MINHOOK_REVISION
  "c3fcafdc10146beb5919319d0683e44e3c30d537"
  CACHE STRING
  "Reviewed MinHook v1.3.4 revision"
)
set(
  TIMETRANSLE_JSON_REVISION
  "65ee68451d8eb2b5f3a30b410476ab83deb3289b"
  CACHE STRING
  "Reviewed nlohmann/json v3.12.0 revision"
)
set(
  TIMETRANSLE_CATCH2_REVISION
  "b8acae26b0453dad6250447ad32f6b0015b162ae"
  CACHE STRING
  "Reviewed Catch2 v3.15.0 revision"
)
set(
  TIMETRANSLE_WEBVIEW2_SDK_VERSION
  "1.0.3856.49"
  CACHE STRING
  "Reviewed Microsoft.Web.WebView2 SDK version"
)

function(timetransle_fetch_dependencies)
  if(NOT TIMETRANSLE_FETCH_DEPENDENCIES)
    message(STATUS "Third-party dependency fetching is disabled.")
    return()
  endif()

  include(FetchContent)

  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG "${TIMETRANSLE_MINHOOK_REVISION}"
    GIT_PROGRESS TRUE
  )
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG "${TIMETRANSLE_JSON_REVISION}"
    GIT_PROGRESS TRUE
  )
  FetchContent_MakeAvailable(minhook nlohmann_json)

  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    FetchContent_Declare(
      webview2
      URL
        "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${TIMETRANSLE_WEBVIEW2_SDK_VERSION}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(webview2)
    set(
      TIMETRANSLE_WEBVIEW2_ROOT
      "${webview2_SOURCE_DIR}"
      CACHE INTERNAL
      "Extracted Microsoft.Web.WebView2 SDK package"
    )
  endif()
endfunction()
