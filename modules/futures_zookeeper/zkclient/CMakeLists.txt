CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
PROJECT(zkclient)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${zkclient_SOURCE_DIR}/cmake)

INCLUDE (AutoconfHelper)

ac_init(zkclient 3.4.9 "" "")
ac_header_time()
ac_header_stdc()
ac_check_headers(arpa/inet.h fcntl.h netdb.h netinet/in.h stdlib.h string.h
    sys/socket.h sys/time.h unistd.h sys/utsname.h)

ac_c_const()
ac_c_inline()

ac_check_funcs(getcwd gethostbyname gethostname getlogin getpwuid_r gettimeofday getuid memmove memset poll socket strchr strdup strerror strtol)

# SET(POLL_NFDS_TYPE "nfds_t")

find_package(Threads)
if (Threads_FOUND AND (NOT MSVC))
  set(HAVE_PTHREAD 1)
endif ()

SET(HAVE_GENERATED_ZOOKEEPER_JUTE_H 1)
SET(HAVE_GENERATED_ZOOKEEPER_JUTE_C 1)
SET(POLL_NFDS_TYPE 1)

configure_file(${zkclient_SOURCE_DIR}/cmake/config.h.cmakein
    ${zkclient_BINARY_DIR}/config.h)

ADD_DEFINITIONS(-D_GNU_SOURCE)
ADD_DEFINITIONS(-DZOO_IPV6_ENABLED)


INCLUDE_DIRECTORIES(${zkclient_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${zkclient_SOURCE_DIR}/generated)
INCLUDE_DIRECTORIES(${zkclient_BINARY_DIR}/)

if (CONFIG_ZKCLIENT_MULTITHREAD)
  ADD_DEFINITIONS(-DTHREADED)
  SET(ADAPTER_SRC src/mt_adaptor.c)
else ()
  SET(ADAPTER_SRC src/st_adaptor.c)
endif()

SET(ALL_SRC src/zookeeper.c src/recordio.c generated/zookeeper.jute.c
    src/zk_log.c src/zk_hashtable.c
    src/hashtable/hashtable_itr.c src/hashtable/hashtable.c
    ${ADAPTER_SRC})

ADD_LIBRARY(zkclient STATIC ${ALL_SRC})
TARGET_LINK_LIBRARIES(zkclient pthread m)
INSTALL(TARGETS zkclient ARCHIVE DESTINATION lib)

# CPPBUILD_INSTALL_LIB(zkclient)
# if (CPPBUILD_IS_TOPLEVEL)
#     SET(CLI_SRC src/cli.c)
#     CPPBUILD_APP_TARGET(cli_mt ${CLI_SRC})
# endif()

