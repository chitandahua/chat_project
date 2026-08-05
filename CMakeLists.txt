cmake_minimum_required(VERSION 3.16)
project(chat_project LANGUAGES CXX)

cmake_policy(SET CMP0167 NEW)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-g -O0 -Wall -Wextra -Werror -Wshadow -Wno-unused-parameter
    -Wno-unused-variable -Wno-unused-function -Wno-missing-field-initializers)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()

find_program(CCACHE_PROGRAM ccache)

if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# ---- 所有 server 都可能用到的依赖,统一在顶层 find_package 一次 ----
find_package(Boost REQUIRED COMPONENTS url)
find_package(nlohmann_json REQUIRED)
find_package(protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(tl-expected REQUIRED)

# ---- 公共依赖打包成一个 INTERFACE 库,子项目按需 link 这一个 target 就够了 ----
add_library(project_common_deps INTERFACE)
target_link_libraries(project_common_deps INTERFACE
    Boost::url
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
    tl::expected
)
target_precompile_headers(project_common_deps INTERFACE
    <boost/asio.hpp>
    <boost/beast/http.hpp>
    <boost/url.hpp>
)

# ---- proto 生成逻辑抽成一个可复用的函数,每个子目录按需调用 ----
set(SHARED_PROTO_DIR ${CMAKE_SOURCE_DIR}/proto)
include(cmake/proto.cmake)

# ---- 下钻各个子项目 ----
add_subdirectory(common)
add_subdirectory(gate_server)
add_subdirectory(verify_server)
add_subdirectory(status_server)