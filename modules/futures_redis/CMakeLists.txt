CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
INCLUDE(${futures_cpp_SOURCE_DIR}/modules/modules.cmake)
FUTURES_MODULE(redis)

INCLUDE_DIRECTORIES(${futures_redis_SOURCE_DIR}/include)
SET(HIREDIS_SRC_DIR src/hiredis)
SET(HIREDIS_SRC
  ${HIREDIS_SRC_DIR}/async.c
  ${HIREDIS_SRC_DIR}/dict.c
  ${HIREDIS_SRC_DIR}/hiredis.c
  ${HIREDIS_SRC_DIR}/net.c
  ${HIREDIS_SRC_DIR}/read.c
  ${HIREDIS_SRC_DIR}/sds.c
)
SET(ALL_SRC src/RedisReply.cpp src/RedisFuture.cpp ${HIREDIS_SRC})

FUTURES_CPP_BUILD_MODULE(${ALL_SRC})
FUTURES_CPP_BUILD_MODULE_EXAMPLE(ex_subscriber examples/subscriber.cpp)

FUTURES_CPP_MODULE_UNITTEST()

