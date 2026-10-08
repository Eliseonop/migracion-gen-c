# gc_log: registro de los ejecutables y de las capas con E/S, sobre spdlog.
# gc_core no lo usa: es lógica pura (solo STL y JSON).

add_library(gc_log src/log.cpp)
add_library(gc::log ALIAS gc_log)

target_include_directories(gc_log PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gc_log PUBLIC spdlog::spdlog)
target_compile_features(gc_log PUBLIC cxx_std_23)
gc_set_warnings(gc_log)
