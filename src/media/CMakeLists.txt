# gc_media: lo que habla con FFmpeg (libav*): sondas, decode, mezcla de audio y encode; y la
# reproducción del audio con miniaudio. Solo se compila si hay FFmpeg (presets msvc-vcpkg-*,
# feature `media` de vcpkg.json).

# La implementación de miniaudio (C de terceros) va aparte, sin nuestros avisos estrictos.
add_library(gc_miniaudio STATIC src/miniaudio_impl.cpp)
target_include_directories(gc_miniaudio SYSTEM PUBLIC "${MINIAUDIO_INCLUDE_DIR}")
if(UNIX AND NOT APPLE)
    target_link_libraries(gc_miniaudio PUBLIC ${CMAKE_DL_LIBS} pthread m)
endif()

add_library(gc_media src/audio.cpp src/audio_out.cpp src/decode.cpp src/encode.cpp src/image.cpp src/matte_input.cpp src/probe.cpp)
add_library(gc::media ALIAS gc_media)
target_link_libraries(gc_media PRIVATE gc_miniaudio)

target_include_directories(gc_media PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# py.hpp (redondeo de Python) es interna de gc_core.
target_include_directories(gc_media PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src")
target_include_directories(gc_media SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(gc_media PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(gc_media PUBLIC gc::core PRIVATE ${FFMPEG_LIBRARIES})
target_compile_features(gc_media PUBLIC cxx_std_23)
gc_set_warnings(gc_media)
