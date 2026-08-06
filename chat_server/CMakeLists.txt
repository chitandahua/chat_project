add_proto_library(message_proto ${SHARED_PROTO_DIR}/message.proto)

file(GLOB_RECURSE CHAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

find_package(Boost REQUIRED COMPONENTS json)
find_package(Boost REQUIRED COMPONENTS charconv)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(chat_server ${CHAT_SOURCES})

target_link_libraries(chat_server
    PRIVATE
    project_common_deps
    message_proto
    common
    Boost::json
    Boost::charconv Threads::Threads OpenSSL::Crypto OpenSSL::SSL)
