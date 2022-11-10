
include_directories(${PROJECT_SOURCE_DIR})
link_directories(${PROJECT_SOURCE_DIR}/lib)

include_directories(/usr/local/include/yaml-cpp)

set(LIBS
    dbspider
    dl
    pthread
    yaml-cpp
)

# echo_server
dbspider_add_executable(echo_server echo_server.cpp dbspider ${LIBS})

# consumer.cpp
dbspider_add_executable(consumer rpc/consumer.cpp dbspider ${LIBS})

# consumer.cpp
dbspider_add_executable(provider rpc/provider.cpp dbspider ${LIBS})

# consumer.cpp
dbspider_add_executable(registry rpc/registry.cpp dbspider ${LIBS})

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin/example)