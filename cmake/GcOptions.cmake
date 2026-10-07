# Opciones de compilación comunes a todos los objetivos de GenClipPro.
#
# Nota MinGW (toolchain provisional hasta tener MSVC): los .exe dependen de las DLL
# de mingw/bin (libstdc++, winpthread). CLion ya las pone en el PATH al ejecutar;
# fuera de CLion hay que añadirlas. `-static` no sirve con la MinGW de CLion 2026.2
# (falla al enlazar winpthread: __intrinsic_setjmpex).

# Sin contracción a FMA (a*b + c en una sola instrucción): cambia el último bit de
# los cálculos y rompería la paridad exacta con Python (y entre x86 y ARM). MSVC no
# contrae por defecto.
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()

# gc_set_warnings(<target>): warnings estrictos solo en NUESTRO código (las
# dependencias se incluyen como SYSTEM y no generan ruido).
function(gc_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
            /w14242 /w14254 /w14263 /w14265 /w14287 /w14296 /w14311 /w14826 /w14928)
        if(GC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align
            -Wunused -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion
            -Wimplicit-fallthrough)
        if(GC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Sanitizers (Clang/GCC fuera de Windows): -DGC_SANITIZE=address;undefined
set(GC_SANITIZE "" CACHE STRING "Sanitizers a activar (p. ej. address;undefined)")
if(GC_SANITIZE AND NOT MSVC AND NOT MINGW)
    list(JOIN GC_SANITIZE "," _gc_san)
    add_compile_options(-fsanitize=${_gc_san} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${_gc_san})
endif()
