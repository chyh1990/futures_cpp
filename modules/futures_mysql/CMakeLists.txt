CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
INCLUDE(${futures_cpp_SOURCE_DIR}/modules/modules.cmake)
FUTURES_MODULE(mysql)

ADD_SUBDIRECTORY(deps/mariadb_connector)

INCLUDE_DIRECTORIES(${futures_mysql_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(${futures_mysql_SOURCE_DIR}/deps/mariadb_connector/include)

SET(ALL_SRC src/futures_mysql.cpp src/Connection.cpp src/PrepareStatement.cpp
  src/Transaction.cpp)

FUTURES_CPP_BUILD_MODULE(${ALL_SRC})
TARGET_LINK_LIBRARIES(futures_mysql mariadbclient)

FUTURES_CPP_MODULE_UNITTEST()

