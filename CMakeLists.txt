CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
PROJECT(futures_cpp)

MACRO(FUTURES_CPP_ENABLE_CXX14)
  if(NOT MSVC)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
  endif()
ENDMACRO()

FUTURES_CPP_ENABLE_CXX14()

OPTION(ENABLE_EXAMPLES "build examples" OFF)
OPTION(ENABLE_UNITTEST "build unit tests" OFF)
OPTION(ENABLE_STATIC_DEPS "disable static dependencies" ON)

# find libs
if (ENABLE_STATIC_DEPS)
  set(BOOST_USE_STATIC_LIBS        ON)
endif ()
find_package(Boost 1.50 REQUIRED)
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})

if (ENABLE_STATIC_DEPS)
  set(OPENSSL_USE_STATIC_LIBS ON)
endif()
find_package(OpenSSL REQUIRED)
INCLUDE_DIRECTORIES(${OPENSSL_INCLUDE_DIR})

ADD_SUBDIRECTORY(${futures_cpp_SOURCE_DIR}/deps/libev)

find_package(ZLIB)
if (ZLIB_FOUND)
  INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIRS})
  ADD_DEFINITIONS(-DCONFIG_FUTURES_CPP_ZLIB)
endif ()

INCLUDE_DIRECTORIES(${futures_cpp_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${futures_cpp_SOURCE_DIR}/deps/libev/include)

AUX_SOURCE_DIRECTORY(src CORE_SRC)
AUX_SOURCE_DIRECTORY(src/folly FOLLY_LITE_SRC)
AUX_SOURCE_DIRECTORY(src/http HTTP_SRC)
ADD_SUBDIRECTORY(src/udns)
SET(ALL_SRC ${CORE_SRC} ${HTTP_SRC} ${FOLLY_LITE_SRC})

if (UNIX)
  SET(LIBAE_SRC src/libae/anet.c)
endif()

SET(ALL_SRC ${ALL_SRC} ${LIBAE_SRC})

ADD_LIBRARY(futures_cpp STATIC ${ALL_SRC})
TARGET_LINK_LIBRARIES(futures_cpp udns ev ${ZLIB_LIBRARIES} ${OPENSSL_LIBRARIES}
  ${Boost_LIBRARIES} pthread dl)
if (ENABLE_ZLIB)
  TARGET_LINK_LIBRARIES(futures_cpp zlibstatic)
endif ()

INSTALL(TARGETS futures_cpp
  ARCHIVE DESTINATION lib
  )
INSTALL(DIRECTORY ${futures_cpp_SOURCE_DIR}/include/futures DESTINATION
  include)

MACRO(FUTURES_CPP_BUILD_EXAMPLE name)
  ADD_EXECUTABLE(${name} ${ARGN})
  TARGET_LINK_LIBRARIES(${name} futures_cpp)
  INSTALL(TARGETS ${name} DESTINATION bin)
ENDMACRO()

if (ENABLE_EXAMPLES)
  FUTURES_CPP_BUILD_EXAMPLE(ex_fake_http examples/fake_http.cpp)
  FUTURES_CPP_BUILD_EXAMPLE(ex_client_test examples/client_test.cpp)
  FUTURES_CPP_BUILD_EXAMPLE(ex_connect_proxy examples/connect_proxy.cpp)
  FUTURES_CPP_BUILD_EXAMPLE(ex_console examples/console.cpp)
  FUTURES_CPP_BUILD_EXAMPLE(ex_ws_server examples/ws_server.cpp)
  FUTURES_CPP_BUILD_EXAMPLE(ex_echo examples/echo.cpp)
endif()

if (ENABLE_TEST)
  ADD_SUBDIRECTORY(${futures_cpp_SOURCE_DIR}/deps/gtest)
  INCLUDE_DIRECTORIES(${futures_cpp_SOURCE_DIR}/deps/gtest/include)
  FILE(GLOB_RECURSE unittest_SRC
    "test/*.cc"
    "test/*.cpp"
    )
  ADD_EXECUTABLE(futures_cpp_test ${unittest_SRC})
  TARGET_LINK_LIBRARIES(futures_cpp_test futures_cpp gtest)
endif()
