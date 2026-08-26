cmake_minimum_required(VERSION 3.8)

add_library(DiscordRPC STATIC
    include/discord_register.h
    include/discord_rpc.h

    src/backoff.h
    src/connection.h
    src/msg_queue.h
    src/rpc_connection.h
    src/serialization.h

    src/connection_win.cpp
    src/discord_register_win.cpp
    src/discord_rpc.cpp
    src/rpc_connection.cpp
    src/serialization.cpp
)

target_include_directories(DiscordRPC
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include

    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)

target_compile_definitions(DiscordRPC
    PRIVATE
        DISCORD_WINDOWS
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
)

if (MSVC)
    target_compile_options(DiscordRPC PRIVATE /EHsc)
endif()

target_link_libraries(DiscordRPC
    PRIVATE
        psapi
        advapi32
)

set_target_properties(DiscordRPC PROPERTIES
    FOLDER "3rd-party"
)