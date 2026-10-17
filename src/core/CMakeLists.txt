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
    # Motor de edición: reglas, comandos, sin solapes, historial; evaluación por instante
    src/audio_export.cpp
    src/bg.cpp
    src/captions.cpp
    src/clip_adjust.cpp
    src/clip_attrs.cpp
    src/clip_filters.cpp
    src/clip_fx.cpp
    src/clip_layout.cpp
    src/clip_motion.cpp
    src/clip_media.cpp
    src/collections.cpp
    src/disk_scan.cpp
    src/edit_commands.cpp
    src/edit_ops.cpp
    src/edit_ops_audio.cpp
    src/edit_ops_compound.cpp
    src/edit_ops_media.cpp
    src/edit_ops_props.cpp
    src/edit_ops_recipes.cpp
    src/export_input.cpp
    src/follow.cpp
    src/font_metrics.cpp
    src/frame.cpp
    src/fx_anims.cpp
    src/fx_catalog.cpp
    src/history_store.cpp
    src/ids.cpp
    src/image_probe.cpp
    src/keyframes.cpp
    src/layers.cpp
    src/library.cpp
    src/masks.cpp
    src/no_overlap.cpp
    src/pose.cpp
    src/projects_store.cpp
    src/shape_geometry.cpp
    src/shapes.cpp
    src/text_events.cpp
    src/text_metrics.cpp
    src/text_style.cpp
    src/py.cpp
    src/session.cpp
    src/settings.cpp
    src/sfx_disk.cpp
    src/sfx_library.cpp
    src/stick_library.cpp
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
