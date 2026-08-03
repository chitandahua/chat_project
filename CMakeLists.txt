cmake_minimum_required(VERSION 3.16)
project(chat_server LANGUAGES CXX)
cmake_policy(SET CMP0167 NEW)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS url)
find_package(nlohmann_json REQUIRED)
find_package(protobuf REQUIRED)
find_package(gRPC REQUIRED)

# ---- 编译 proto,生成文件放在 build 目录下,不写回源码树 ----
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gate_server/proto)
set(PROTO_FILE ${PROTO_DIR}/message.proto)

# 放在 build/generated_proto/ 下,跟源码目录彻底分开
set(PROTO_OUT ${CMAKE_BINARY_DIR}/generated_proto)
file(MAKE_DIRECTORY ${PROTO_OUT})

set(PROTO_GENERATED
    ${PROTO_OUT}/message.pb.cc
    ${PROTO_OUT}/message.pb.h
    ${PROTO_OUT}/message.grpc.pb.cc
    ${PROTO_OUT}/message.grpc.pb.h
)

add_custom_command(
    OUTPUT ${PROTO_GENERATED}
    COMMAND protobuf::protoc
    ARGS --cpp_out=${PROTO_OUT}
    --grpc_out=${PROTO_OUT}
    --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
    -I ${PROTO_DIR}
    ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating C++/gRPC code from message.proto into ${PROTO_OUT}"
    VERBATIM
)

add_custom_target(generate_proto DEPENDS ${PROTO_GENERATED})

# ---- 编译各个 server ----
foreach(server gate_server)
    file(GLOB_RECURSE SERVER_SOURCES
        ${server}/*.cpp
    )

    add_executable(${server}
        ${SERVER_SOURCES}
        ${PROTO_GENERATED}
    )

    add_dependencies(${server} generate_proto)

    # 生成的头文件(message.pb.h / message.grpc.pb.h)在 build/generated_proto/ 下,
    # 项目里 #include "message.pb.h" 需要这条 include path 才能找到
    target_include_directories(${server}
        PRIVATE
        ${PROTO_OUT}
    )

    target_link_libraries(${server}
        PRIVATE
        Boost::url
        nlohmann_json::nlohmann_json
        protobuf::libprotobuf
        gRPC::grpc++
    )
endforeach()