cmake_minimum_required(VERSION 3.28...4.3)

project(GenClipPro
    VERSION 0.1.0
    DESCRIPTION "Editor de clips verticales: motor C++ multiplataforma (migración de video-yt)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Sin módulos de C++20: no se usan, y su escaneo (p1689) añade opciones de GCC que
# clang-tidy y clangd no entienden.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(GC_BUILD_TESTS "Compilar los tests" ${PROJECT_IS_TOP_LEVEL})
option(GC_BUILD_CLI "Compilar la herramienta de línea de órdenes genclip" ON)
option(GC_BUILD_STUDIO "Compilar el editor genclip-studio si hay Qt 6" ON)
option(GC_WARNINGS_AS_ERRORS "Tratar los warnings como errores (CI)" OFF)

include(GcOptions)
include(GcDependencies)

add_subdirectory(src/core)
add_subdirectory(src/log)
if(FFMPEG_FOUND)
    add_subdirectory(src/media)
endif()
if(unofficial-skia_FOUND)
    add_subdirectory(src/render)
endif()
if(TARGET gc_media AND TARGET gc_render)
    add_subdirectory(src/export)
endif()

if(GC_BUILD_CLI)
    add_subdirectory(apps/cli)
endif()

if(GC_BUILD_STUDIO)
    find_package(Qt6 6.4 QUIET COMPONENTS Quick QuickControls2)
    # Un Qt compilado con libstdc++ (el de las distribuciones Linux) no enlaza con libc++.
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("#include <version>\n#ifndef _LIBCPP_VERSION\n#error\n#endif\nint main() {}" GC_LIBCXX)
    if(Qt6_FOUND AND GC_LIBCXX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "Qt ${Qt6_VERSION} encontrado, pero con libc++ no enlaza: genclip-studio no se compila")
    elseif(Qt6_FOUND)
        message(STATUS "Qt ${Qt6_VERSION} encontrado: se compila genclip-studio")
        add_subdirectory(apps/studio)
    endif()
endif()

if(GC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
