# Copyright (C) 2007-2013 LuaDist.
# Created by Peter Drahoš, Peter Kapec
# Redistribution and use of this file is allowed according to the terms of the MIT license.
# For details see the COPYRIGHT file distributed with LuaDist.
# Please note that the package source code is licensed under its own license.

project ( libev C )
cmake_minimum_required ( VERSION 2.8 )
include (config.cmake)

configure_file ( ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h )
include_directories( ${CMAKE_CURRENT_BINARY_DIR} )
include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/include)

set ( EV_SRC
  src/ev.c
  src/event.c
)

if (WIN32)
  list ( APPEND EV_LIBS Ws2_32 )
endif ()

if (HAVE_LIBRT)
  list (APPEND EV_LIBS rt)
endif()

add_library ( ev ${EV_SRC} )
target_link_libraries ( ev ${EV_LIBS} )
