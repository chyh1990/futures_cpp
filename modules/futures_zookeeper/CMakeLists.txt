CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
INCLUDE(${futures_cpp_SOURCE_DIR}/modules/modules.cmake)
FUTURES_MODULE(zookeeper)

ADD_SUBDIRECTORY(zkclient)

INCLUDE_DIRECTORIES(${futures_zookeeper_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${futures_zookeeper_SOURCE_DIR}/zkclient/include)
SET(ALL_SRC src/ZkClient.cpp src/Exception.cpp)

FUTURES_CPP_BUILD_MODULE(${ALL_SRC})
TARGET_LINK_LIBRARIES(futures_zookeeper zkclient)

FUTURES_CPP_BUILD_MODULE_EXAMPLE(ex_zkdlock examples/dlock.cpp)

INCLUDE_DIRECTORIES(${FUTURES_CPP_MODULE_ROOT}/futures_readline/include)
FUTURES_CPP_BUILD_MODULE_EXAMPLE(ex_zkcli examples/cli.cpp)
TARGET_LINK_LIBRARIES(ex_zkcli futures_readline)

FUTURES_CPP_MODULE_UNITTEST()
