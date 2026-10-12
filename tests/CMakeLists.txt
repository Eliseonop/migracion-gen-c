add_executable(gc_tests
    support/json_diff.cpp
    unit/test_clip_media.cpp
    unit/test_coercion.cpp
    unit/test_export_total.cpp
    unit/test_frame.cpp
    unit/test_fx_catalog.cpp
    unit/test_keyframes.cpp
    unit/test_layers.cpp
    unit/test_log.cpp
    unit/test_migrations.cpp
    unit/test_model.cpp
    unit/test_projects_store.cpp
    unit/test_py.cpp
    unit/test_session.cpp
    unit/test_sfx.cpp
    parity/bench_edit.cpp
    parity/test_audio_parity.cpp
    parity/test_chroma_parity.cpp
    parity/test_collections_parity.cpp
    parity/test_edit_parity.cpp
    parity/test_eval_parity.cpp
    parity/test_frame_parity.cpp
    parity/test_history_parity.cpp
    parity/test_json_parity.cpp
    parity/test_keyframes_parity.cpp
    parity/test_layers_parity.cpp
    parity/test_layout_parity.cpp
    parity/test_library_parity.cpp
    parity/test_model_parity.cpp
    parity/test_projects_parity.cpp
    parity/test_settings_parity.cpp
    parity/test_sfx_disk_parity.cpp
    parity/test_stick_parity.cpp
    parity/test_text_parity.cpp
    parity/test_motion_parity.cpp
    parity/test_media_parity.cpp
    parity/test_export_input_parity.cpp
)
# support/: utilidades de los tests. src/core/src: tests de caja blanca de piezas internas (py.hpp).
target_include_directories(gc_tests PRIVATE support "${PROJECT_SOURCE_DIR}/src/core/src")
target_link_libraries(gc_tests PRIVATE gc::core gc::log Catch2::Catch2WithMain)
# Fixtures: tests/fixtures/model (al repo) y tests/fixtures/local (tus datos, ignorado por git).
target_compile_definitions(gc_tests PRIVATE
    GC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    GC_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/resources")
gc_set_warnings(gc_tests)
# Con FFmpeg (vcpkg), los tests de gc_media.
if(TARGET gc_media)
    target_sources(gc_tests PRIVATE parity/test_probe_parity.cpp parity/test_decode_parity.cpp
                   parity/test_seek_parity.cpp)
    target_link_libraries(gc_tests PRIVATE gc::media)
endif()
# Con Skia (vcpkg), los de gc_render.
if(TARGET gc_render)
    target_sources(gc_tests PRIVATE unit/test_render_canvas.cpp unit/test_render_compose.cpp)
    target_link_libraries(gc_tests PRIVATE gc::render)
    # La paridad del render lee el material y los fotogramas del export con gc_media.
    if(TARGET gc_media)
        target_sources(gc_tests PRIVATE parity/test_render_parity.cpp parity/test_text_render_parity.cpp)
    endif()
endif()
# MSVC sabe que FAIL() lanza y da por inalcanzable el `return`/`throw` que GCC necesita detrás.
# El manifiesto pone argv en UTF-8: si no, con una página de códigos ANSI que no lo sea (la del
# CI), Catch2 no encuentra los tests con tildes que ctest le pide por nombre.
if(MSVC)
    target_compile_options(gc_tests PRIVATE /wd4702)
    target_sources(gc_tests PRIVATE utf8.manifest)
endif()

if(catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)
catch_discover_tests(gc_tests DISCOVERY_MODE PRE_TEST)

# genclip: los errores salen por stderr con el nivel por defecto y con el registro apagado.
if(TARGET genclip)
    foreach(level IN ITEMS "" off)
        add_test(NAME "genclip: un error sale por stderr (GENCLIP_LOG=${level})"
            COMMAND "${CMAKE_COMMAND}" -DGENCLIP=$<TARGET_FILE:genclip> -DLOG=${level}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cli/errores.cmake")
    endforeach()
endif()
