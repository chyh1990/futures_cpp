CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
INCLUDE(${futures_cpp_SOURCE_DIR}/modules/modules.cmake)
FUTURES_MODULE(readline)

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
  ${futures_readline_SOURCE_DIR}/cmake)
FIND_PACKAGE(Readline REQUIRED)

INCLUDE_DIRECTORIES(${Readline_INCLUDE_DIR})
MESSAGE(STATUS ${Readline_INCLUDE_DIR})
INCLUDE_DIRECTORIES(${futures_readline_SOURCE_DIR}/include)
SET(ALL_SRC src/Readline.cpp src/Console.cpp)

FUTURES_CPP_BUILD_MODULE(${ALL_SRC})
TARGET_LINK_LIBRARIES(futures_readline ${Readline_LIBRARY})
FUTURES_CPP_BUILD_MODULE_EXAMPLE(ex_readline examples/console.cpp)

FUTURES_CPP_MODULE_UNITTEST()
