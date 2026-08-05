add_proto_library(message_proto ${SHARED_PROTO_DIR}/message.proto)

file(GLOB_RECURSE GATE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

find_package(Boost REQUIRED COMPONENTS json)
find_package(Boost REQUIRED COMPONENTS charconv)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(gate_server ${GATE_SOURCES})

target_link_libraries(gate_server
    PRIVATE
    project_common_deps # 公共依赖,一个 target 全带上
    message_proto
    Boost::json
    Boost::charconv Threads::Threads OpenSSL::Crypto OpenSSL::SSL)
