# Dependencias de terceros.
#
# Cada una se busca primero con find_package (la instala vcpkg cuando se usa un
# preset *-vcpkg, ver vcpkg.json) y, si no está, FetchContent la descarga y
# compila con el propio proyecto. Así compila igual con la MinGW de CLion, con
# MSVC + vcpkg o en CI sin preparar nada.
#
# Las versiones de aquí y las de vcpkg.json deben ir a la par.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    SYSTEM
    FIND_PACKAGE_ARGS 3.11 CONFIG)
FetchContent_MakeAvailable(nlohmann_json)

# Registro (gc_log). Con el fmt que trae dentro; con vcpkg, el de vcpkg.
FetchContent_Declare(spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
    URL_HASH SHA256=15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67
    SYSTEM
    FIND_PACKAGE_ARGS 1.12 CONFIG)
FetchContent_MakeAvailable(spdlog)

# FFmpeg (gc_media): solo con vcpkg (feature `media`), sin FetchContent. Sin él no se compila
# gc_media y el resto sigue igual (la MinGW de CLion no lo tiene).
find_package(FFMPEG QUIET)
if(FFMPEG_FOUND)
    message(STATUS "FFmpeg encontrado: se compila gc_media")
endif()

if(GC_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.9.1.tar.gz
        URL_HASH SHA256=a215c2a723bd7483efd236dc86066842a389cb4e344c61119c978acdf24d39be
        SYSTEM
        FIND_PACKAGE_ARGS 3 CONFIG)
    FetchContent_MakeAvailable(Catch2)
endif()
