# gc_core: modelo de datos, formato JSON del proyecto y lógica de edición.
# Sin Qt, sin GPU, sin dependencias de plataforma: corre igual en todos los SO.

configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/gc/core/version.hpp" @ONLY)

add_library(gc_core
    # Documento: modelo, formato JSON, migraciones
    src/error.cpp
    src/io.cpp
    src/json_reader.cpp
    src/migrations.cpp
    src/serialize.cpp
    # Motor de edición: reglas, comandos, sin solapes, historial
    src/bg.cpp
    src/captions.cpp
    src/clip_attrs.cpp
    src/clip_layout.cpp
    src/clip_motion.cpp
    src/edit_commands.cpp
    src/edit_ops.cpp
    src/edit_ops_audio.cpp
    src/edit_ops_compound.cpp
    src/edit_ops_media.cpp
    src/edit_ops_props.cpp
    src/follow.cpp
    src/fx_catalog.cpp
    src/ids.cpp
    src/keyframes.cpp
    src/masks.cpp
    src/no_overlap.cpp
    src/pose.cpp
    src/shapes.cpp
    src/py.cpp
    src/session.cpp
    src/timeline_rules.cpp
)
add_library(gc::core ALIAS gc_core)

target_include_directories(gc_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(gc_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(gc_core PUBLIC cxx_std_23)
gc_set_warnings(gc_core)
