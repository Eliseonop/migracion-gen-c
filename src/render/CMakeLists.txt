# gc_render: el compositor del fotograma (las capas de frame.hpp dibujadas con Skia por CPU).
# Solo se compila si hay Skia (presets msvc-vcpkg-*, feature `render` de vcpkg.json).

find_package(harfbuzz CONFIG REQUIRED)
# FreeType: la medida del texto como Pillow (src/pillow_font.cpp). Es exacta con la misma versión
# que el Pillow de video-yt (2.14.3).
find_package(Freetype REQUIRED)

add_library(gc_render src/canvas.cpp src/compose.cpp src/effects.cpp src/mask.cpp src/png.cpp src/pillow_font.cpp
            src/shape.cpp src/text.cpp src/vfx.cpp src/paper.cpp)
add_library(gc::render ALIAS gc_render)

target_include_directories(gc_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# py.hpp de gc_core (float() e int() de Python, privados del núcleo): el texto lee el estilo igual
# que text_ass.py.
target_include_directories(gc_render PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src")
target_link_libraries(gc_render PUBLIC gc::core PRIVATE unofficial::skia::skia harfbuzz::harfbuzz Freetype::Freetype)
target_compile_features(gc_render PUBLIC cxx_std_23)
gc_set_warnings(gc_render)
