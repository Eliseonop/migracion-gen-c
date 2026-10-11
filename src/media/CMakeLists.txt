# gc_media: lo que habla con FFmpeg (libav*): sondas, y más adelante demux, decode y encode.
# Solo se compila si hay FFmpeg (presets msvc-vcpkg-*, feature `media` de vcpkg.json).

add_library(gc_media src/probe.cpp)
add_library(gc::media ALIAS gc_media)

target_include_directories(gc_media PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# py.hpp (redondeo de Python) es interna de gc_core.
target_include_directories(gc_media PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src")
target_include_directories(gc_media SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(gc_media PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(gc_media PUBLIC gc::core PRIVATE ${FFMPEG_LIBRARIES})
target_compile_features(gc_media PUBLIC cxx_std_23)
gc_set_warnings(gc_media)
