
CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
INCLUDE(cppbuild_common)
PROJECT(futures_cpp)

CPPBUILD_ENABLE_CXX11()
CPPBUILD_DEPEND()

INCLUDE_DIRECTORIES(${futures_cpp_SOURCE_DIR}/include)
AUX_SOURCE_DIRECTORY(src ALL_SRC)

if (UNIX)
  SET(LIBAE_SRC src/libae/anet.c)
endif()

SET(ALL_SRC ${ALL_SRC} ${LIBAE_SRC})

if(IOS)
CPPBUILD_MANGLELIB_TARGET(futures_cpp ${ALL_SRC})
else()
CPPBUILD_LIB_TARGET(futures_cpp ${ALL_SRC})
endif()
CPPBUILD_INSTALL_LIB(futures_cpp)

CPPBUILD_APP_TARGET(ex_socket examples/socket.cpp)
CPPBUILD_APP_TARGET(ex_fake_http examples/fake_http.cpp)

CPPBUILD_UNITTEST()
