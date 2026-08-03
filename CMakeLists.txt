cmake_minimum_required(VERSION 3.16)
project(chat_server LANGUAGES CXX)

cmake_policy(SET CMP0167 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS url)

foreach(server gate_server)
    file(GLOB_RECURSE SERVER_SOURCES
        ${server}/*.cpp
    )

    add_executable(${server}
        ${SERVER_SOURCES}
    )

    target_link_libraries(${server}
        PRIVATE
        Boost::url
    )
endforeach()
