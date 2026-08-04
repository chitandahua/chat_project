# 用法:add_proto_library(<target_name> <proto文件路径>)
# 如果 <target_name> 这个 target 已经存在(说明别的子目录已经声明过同一份 proto),
# 直接跳过生成流程,后续 target_link_libraries(xxx PRIVATE <target_name>) 一样能用。
function(add_proto_library TARGET_NAME PROTO_FILE)
    if(TARGET ${TARGET_NAME})
        message(STATUS "${TARGET_NAME} already exists, reusing it instead of regenerating")
        return()
    endif()

    get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

    set(PROTO_OUT ${CMAKE_BINARY_DIR}/generated_proto/${TARGET_NAME})
    file(MAKE_DIRECTORY ${PROTO_OUT})

    set(PROTO_GENERATED
        ${PROTO_OUT}/${PROTO_NAME}.pb.cc
        ${PROTO_OUT}/${PROTO_NAME}.pb.h
        ${PROTO_OUT}/${PROTO_NAME}.grpc.pb.cc
        ${PROTO_OUT}/${PROTO_NAME}.grpc.pb.h
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
        COMMENT "Generating C++/gRPC code for ${TARGET_NAME} from ${PROTO_FILE}"
        VERBATIM
    )

    add_library(${TARGET_NAME} STATIC ${PROTO_GENERATED})
    target_link_libraries(${TARGET_NAME} PUBLIC protobuf::libprotobuf gRPC::grpc++)
    target_include_directories(${TARGET_NAME} PUBLIC ${PROTO_OUT})
endfunction()