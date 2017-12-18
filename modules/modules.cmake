SET(FUTURES_CPP_MODULE_ROOT ${FUTURES_CPP_ROOT}/modules)

MACRO(FUTURES_MODULE name)
  PROJECT(futures_${name})
  SET(cur_futures_cpp_mod "futures_${name}")
ENDMACRO()

MACRO(FUTURES_CPP_BUILD_MODULE)
  MESSAGE(STATUS "Building module: ${cur_futures_cpp_mod}")
  INCLUDE_DIRECTORIES(${FUTURES_CPP_ROOT}/include)
  ADD_LIBRARY(${cur_futures_cpp_mod} STATIC ${ARGN})
  TARGET_LINK_LIBRARIES(${cur_futures_cpp_mod} futures_cpp)
  INSTALL(TARGETS ${cur_futures_cpp_mod} ARCHIVE DESTINATION lib)
  INSTALL(DIRECTORY include/${cur_futures_cpp_mod} DESTINATION include)
ENDMACRO()

MACRO(FUTURES_CPP_MODULE_UNITTEST)
  if (ENABLE_TEST)
    INCLUDE_DIRECTORIES(${FUTURES_CPP_ROOT}/deps/gtest/include)
    FILE(GLOB_RECURSE unittest_SRC
      "test/*.cc"
      "test/*.cpp"
      )
    ADD_EXECUTABLE(${cur_futures_cpp_mod}_test ${unittest_SRC})
    TARGET_LINK_LIBRARIES(${cur_futures_cpp_mod}_test ${cur_futures_cpp_mod} futures_cpp gtest)
  endif()
ENDMACRO()

MACRO(FUTURES_CPP_BUILD_MODULE_EXAMPLE name)
  if (ENABLE_EXAMPLES)
    ADD_EXECUTABLE(${name} ${ARGN})
    TARGET_LINK_LIBRARIES(${name} ${cur_futures_cpp_mod} futures_cpp)
    INSTALL(TARGETS ${name} DESTINATION bin)
  endif()
ENDMACRO()


