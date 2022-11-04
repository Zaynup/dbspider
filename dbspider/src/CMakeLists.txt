include_directories(${PROJECT_SOURCE_DIR})

set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic  -std=c++20  -Wall -Werror -Wno-deprecated -Wno-unused-function ")

set(LIB_SRC
    base/thread.cc

    sync/mutex.cc
)

set(LIB_LIB pthread yaml-cpp dl)

add_library(dbspider SHARED ${LIB_SRC})
target_link_libraries(dbspider ${LIB_LIB})

redefine_file_macro(dbspider)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)