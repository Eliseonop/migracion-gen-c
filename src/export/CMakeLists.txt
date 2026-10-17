# gc_export: el export de una timeline a MP4 (evaluar → componer → codificar) y la carga de un
# proyecto de video-yt con su material. Lo usan `genclip frame` y `genclip render`, y lo usará el
# editor (F5). Solo con FFmpeg y Skia (presets msvc-vcpkg-*).

add_library(gc_export src/project.cpp src/render.cpp)
add_library(gc::export ALIAS gc_export)

target_include_directories(gc_export PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gc_export PUBLIC gc::core gc::media gc::render)
target_compile_features(gc_export PUBLIC cxx_std_23)
gc_set_warnings(gc_export)
