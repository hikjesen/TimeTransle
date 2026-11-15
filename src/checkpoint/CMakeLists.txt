if(CMAKE_SIZEOF_VOID_P EQUAL 4)
 add_library(timetransle_checkpoint STATIC src/runtime.cpp src/arena.cpp src/support.cpp src/graphics.cpp src/graphics_contents.cpp src/audio.cpp)
 target_link_libraries(timetransle_checkpoint PRIVATE d3d9 dsound dxguid)
 # Keep the reference implementation's original casts; don't spend the MVP rewriting its style.
 target_compile_options(timetransle_checkpoint PRIVATE /W3 /WX- /utf-8)
 set_target_properties(timetransle_checkpoint PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
else()
 add_library(timetransle_checkpoint STATIC src/stub.cpp)
endif()
add_library(TimeTransle::Checkpoint ALIAS timetransle_checkpoint)
target_include_directories(timetransle_checkpoint PUBLIC include)
target_link_libraries(timetransle_checkpoint PUBLIC TimeTransle::Ipc TimeTransle::VirtualClock)

