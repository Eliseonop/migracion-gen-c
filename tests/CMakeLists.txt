add_executable(gc_tests
    support/json_diff.cpp
    unit/test_coercion.cpp
    unit/test_frame.cpp
    unit/test_keyframes.cpp
    unit/test_layers.cpp
    unit/test_migrations.cpp
    unit/test_model.cpp
    unit/test_py.cpp
    unit/test_session.cpp
    unit/test_sfx.cpp
    parity/bench_edit.cpp
    parity/test_edit_parity.cpp
    parity/test_eval_parity.cpp
    parity/test_frame_parity.cpp
    parity/test_history_parity.cpp
    parity/test_keyframes_parity.cpp
    parity/test_layers_parity.cpp
    parity/test_layout_parity.cpp
    parity/test_library_parity.cpp
    parity/test_model_parity.cpp
    parity/test_settings_parity.cpp
)
# support/: utilidades de los tests. src/core/src: tests de caja blanca de piezas internas (py.hpp).
target_include_directories(gc_tests PRIVATE support "${PROJECT_SOURCE_DIR}/src/core/src")
target_link_libraries(gc_tests PRIVATE gc::core Catch2::Catch2WithMain)
# Fixtures: tests/fixtures/model (al repo) y tests/fixtures/local (tus datos, ignorado por git).
target_compile_definitions(gc_tests PRIVATE
    GC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    GC_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/resources")
gc_set_warnings(gc_tests)

if(catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)
catch_discover_tests(gc_tests DISCOVERY_MODE PRE_TEST)
