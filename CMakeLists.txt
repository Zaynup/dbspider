# 设置cmake的最低版本和项目名称
cmake_minimum_required(VERSION 3.0)
project(dbspider)

# 生成debug版本，可以进行gdb调试
set(CMAKE_BUILD_TYPE "Debug")

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic  -std=c++20  -Wall -Werror -Wno-deprecated -Wno-unused-function ")

# 设置项目可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 设置项目库文件输出的路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

include(cmake/utils.cmake)
add_definitions(-Wno-builtin-macro-redefined)

# 设置项目编译头文件搜索路径 -I
include_directories(${PROJECT_SOURCE_DIR}/dbspider/include)
include_directories(${PROJECT_SOURCE_DIR}/dbspider/include/base)
include_directories(${PROJECT_SOURCE_DIR}/dbspider/include/sync)
include_directories(${PROJECT_SOURCE_DIR}/dbspider/include/net)
include_directories(${PROJECT_SOURCE_DIR}/dbspider/include/rpc)

include_directories(${PROJECT_SOURCE_DIR}/example)

# 设置项目库文件搜索路径 -L
link_directories(${PROJECT_SOURCE_DIR}/lib)

# src包含了dbspider框架所有的相关代码
add_subdirectory(dbspider/src)

# tests包含了dbspider框架使用的测试代码
add_subdirectory(tests)

# example包含了dbspider框架使用的示例代码
add_subdirectory(example)
