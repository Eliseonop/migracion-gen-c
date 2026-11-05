# El Qt de vcpkg en Windows deja sus plugins (el de la plataforma, los de imagen) y sus módulos QML en
# vcpkg_installed, y un ejecutable no los encuentra solo: en Linux, el Qt del sistema sabe dónde
# están. gc_qt_runtime(<target>) pone junto al ejecutable un qt.conf que apunta allí y copia las DLL
# de Qt que cargan esos plugins (las que enlaza el propio ejecutable ya las copia vcpkg). Es para
# compilar y probar desde build/; el instalador hará su propio despliegue.
function(gc_qt_runtime target)
    if(NOT WIN32 OR NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_TARGET_TRIPLET)
        return()
    endif()
    set(root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    file(GLOB release_dlls "${root}/bin/Qt6*.dll")
    file(GLOB debug_dlls "${root}/debug/bin/Qt6*d.dll")
    if(NOT release_dlls OR NOT debug_dlls)
        return()
    endif()
    # En Debug, los plugins y las DLL «d» de vcpkg_installed/<triplete>/debug.
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:${target}>/qt.conf"
         CONTENT "[Paths]\nPrefix=${root}$<$<CONFIG:Debug>:/debug>\nPlugins=Qt6/plugins\nQmlImports=Qt6/qml\n")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<IF:$<CONFIG:Debug>,${debug_dlls},${release_dlls}>"
                "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS VERBATIM)
endfunction()
