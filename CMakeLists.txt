cmake_minimum_required(VERSION 3.16)

# ---- 优先用 clang++,没有就退回默认(通常是 gcc)----
# 必须放在 project() 之前;如果用户在命令行显式指定过
# -DCMAKE_CXX_COMPILER=xxx,这里不会覆盖用户的选择
if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(CLANGXX_PROGRAM clang++)

    if(CLANGXX_PROGRAM)
        set(CMAKE_CXX_COMPILER ${CLANGXX_PROGRAM})
        message(STATUS "clang++ found, using it: ${CLANGXX_PROGRAM}")
    else()
        message(STATUS "clang++ not found, falling back to default compiler")
    endif()
endif()

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

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- 根据最终实际选中的编译器,分别加对应的报错优化参数 ----
# 这里要用 CMAKE_CXX_COMPILER_ID,project() 跑完之后才会有这个值,
# 它反映的是"实际生效"的编译器,不是你上面 set 的那个路径字符串
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "compiling with GCC, adding readability flags")
    add_compile_options(-fdiagnostics-color=always -fno-elide-type -fdiagnostics-show-template-tree)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "compiling with Clang")
    add_compile_options(-fcolor-diagnostics) # clang 默认报错可读性已经不错,这个只是保证有颜色输出
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
add_subdirectory(chat_server)