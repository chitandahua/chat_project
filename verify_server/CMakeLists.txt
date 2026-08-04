add_proto_library(message_proto ${SHARED_PROTO_DIR}/message.proto)
file(GLOB_RECURSE VERIFY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(verify_server ${VERIFY_SOURCES})

target_link_libraries(verify_server
    PRIVATE
    project_common_deps
    message_proto
)