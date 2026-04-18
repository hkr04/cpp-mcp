set(TARGET mcp)

add_library(${TARGET} STATIC
    ../include/mcp_client.h
    mcp_message.cpp
    ../include/mcp_message.h
    mcp_resource.cpp
    ../include/mcp_resource.h
    mcp_server.cpp
    ../include/mcp_server.h
    mcp_tool.cpp
    ../include/mcp_tool.h
    mcp_stdio_client.cpp
    ../include/mcp_stdio_client.h
    mcp_sse_client.cpp
    ../include/mcp_sse_client.h
)

target_compile_definitions(${TARGET} PUBLIC
    MCP_MAX_SESSIONS=${MCP_MAX_SESSIONS}
    MCP_SESSION_TIMEOUT=${MCP_SESSION_TIMEOUT}
)

target_link_libraries(${TARGET} PUBLIC ${CMAKE_THREAD_LIBS_INIT})

# If OpenSSL is found, link the OpenSSL libraries
if(OPENSSL_FOUND)
    target_link_libraries(${TARGET} PUBLIC ${OPENSSL_LIBRARIES})
endif()
