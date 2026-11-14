cmake_minimum_required(VERSION 3.10)
project(IOT_Data_Node)
set(CMAKE_CXX_STANDARD 17)
# 默认Debug版本
set(CMAKE_BUILD_TYPE Debug)

# 查找 libcurl
find_package(CURL REQUIRED)

# 源文件
set(SOURCES
    src/main.cpp
    src/thread_pool.cpp
    src/cache.cpp
)

add_executable(gateway ${SOURCES})

target_include_directories(gateway PRIVATE 
    include
    ${CURL_INCLUDE_DIRS}
)

# 链接pthread库
#target_link_libraries(gateway pthread)
# 添加curl库
target_link_libraries(gateway 
    curl
    pthread
    sqlite3
)

# 复制config.json 配置文件
configure_file(${CMAKE_SOURCE_DIR}/config.json ${CMAKE_BINARY_DIR}/config.json COPYONLY)
