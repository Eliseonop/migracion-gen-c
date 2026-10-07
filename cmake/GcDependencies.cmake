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

if(GC_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.9.1.tar.gz
        URL_HASH SHA256=a215c2a723bd7483efd236dc86066842a389cb4e344c61119c978acdf24d39be
        SYSTEM
        FIND_PACKAGE_ARGS 3 CONFIG)
    FetchContent_MakeAvailable(Catch2)
endif()
