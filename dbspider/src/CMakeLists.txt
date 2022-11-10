include_directories(${PROJECT_SOURCE_DIR})

set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic  -std=c++20  -Wall -Werror -Wno-deprecated -Wno-unused-function ")

set(LIB_SRC
    base/util.cc
    base/log.cc
    base/config.cc
    base/thread.cc
    base/fiber.cc
    base/timer.cc
    base/scheduler.cc
    base/io_manager.cc
    base/fd_manager.cc
    base/hook.cc
    base/lexical_cast.cc
    base/bytearray.cc
    base/stream.cc

    sync/mutex.cc
    sync/co_condvar.cc

    net/address.cc
    net/socket_stream.cc
    net/socket.cc
    net/tcp_server.cc
)

set(LIB_LIB pthread yaml-cpp dl)

add_library(dbspider SHARED ${LIB_SRC})
target_link_libraries(dbspider ${LIB_LIB})

redefine_file_macro(dbspider)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)