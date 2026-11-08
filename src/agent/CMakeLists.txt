# gc_agent: el servidor MCP por HTTP (y, más adelante, el chat de F7) sobre gc_core.

add_library(gc_agent STATIC src/mcp_http_server.cpp)
add_library(gc::agent ALIAS gc_agent)
target_include_directories(gc_agent PUBLIC include)
target_link_libraries(gc_agent PUBLIC gc::core PRIVATE httplib::httplib)
gc_set_warnings(gc_agent)
if(WIN32)
    # Windows 10: lo pide cpp-httplib y hay que decirlo con MinGW.
    target_compile_definitions(gc_agent PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(gc_agent PRIVATE ws2_32)
endif()
