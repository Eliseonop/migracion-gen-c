# genclip: un error sale con código 1 y «error: …» en stderr con cualquier nivel de registro,
# también con GENCLIP_LOG=off (quien lanza genclip, como video-yt en F3, lee stderr).
#
#   cmake -DGENCLIP=<genclip> -DLOG=<nivel o vacío> -P errores.cmake

set(ENV{GENCLIP_LOG} "${LOG}")
execute_process(COMMAND "${GENCLIP}" projects "${CMAKE_CURRENT_LIST_DIR}/no-existe.json"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 1)
    message(FATAL_ERROR "código ${rc}, se esperaba 1\nstdout: ${out}\nstderr: ${err}")
endif()
if(NOT err MATCHES "^error: [^\n]*no-existe\\.json")
    message(FATAL_ERROR "stderr no es «error: …no-existe.json»: «${err}»")
endif()
