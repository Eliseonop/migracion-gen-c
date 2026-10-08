# genclip: herramienta de línea de órdenes del motor (render, comprobaciones, paridad).

add_executable(genclip main.cpp)
target_link_libraries(genclip PRIVATE gc::core gc::log)
if(WIN32)
    target_link_libraries(genclip PRIVATE shell32) # CommandLineToArgvW: argumentos en UTF-8
endif()
gc_set_warnings(genclip)
