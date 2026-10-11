# gc_render: el compositor del fotograma (las capas de frame.hpp dibujadas con Skia por CPU).
# Solo se compila si hay Skia (presets msvc-vcpkg-*, feature `render` de vcpkg.json).

add_library(gc_render src/canvas.cpp src/compose.cpp src/png.cpp)
add_library(gc::render ALIAS gc_render)

target_include_directories(gc_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gc_render PUBLIC gc::core PRIVATE unofficial::skia::skia)
target_compile_features(gc_render PUBLIC cxx_std_23)
gc_set_warnings(gc_render)
