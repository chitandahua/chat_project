# 收集源文件
file(GLOB_RECURSE COMMON_SOURCES ./*.cpp)

# 创建静态库
add_library(common STATIC ${COMMON_SOURCES})

# 设置头文件路径（PUBLIC 让依赖者也能看到）
target_include_directories(common
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/
)

# add_library(common INTERFACE)
#
# target_include_directories(common
# INTERFACE
# ${CMAKE_CURRENT_SOURCE_DIR}
# )

# 如果有外部依赖，也在这里声明
# target_link_libraries(common PUBLIC some_lib)
