add_proto_library(message_proto ${SHARED_PROTO_DIR}/message.proto)
file(GLOB_RECURSE STATUS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(status_server ${STATUS_SOURCES})

# find_package(Boost REQUIRED COMPONENTS redis)
target_link_libraries(status_server
    PRIVATE
    project_common_deps
    message_proto
    common
)