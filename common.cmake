set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb3 -fno-omit-frame-pointer -Wall -Wextra -Werror -Wnon-virtual-dtor -Woverloaded-virtual -Wold-style-cast -std=c++0x -fmax-errors=3")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.9")
    message(FATAL_ERROR "gcc >= 4.9 required for regex support")
  endif()
endif()

option(ENVOY_DEBUG "build debug binaries" ON)
option(ENVOY_CODE_COVERAGE "build with code coverage intrumentation" OFF)

if (ENVOY_CODE_COVERAGE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
  add_definitions(-DCOVERAGE)
endif()

if (ENVOY_DEBUG AND NOT ENVOY_CODE_COVERAGE)
  add_definitions(-DDEBUG)
else()
  add_definitions(-DNDEBUG)
endif()

if (ENVOY_DEBUG OR ENVOY_CODE_COVERAGE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
endif()

option(ENVOY_SANITIZE "build with address sanitizer" OFF)
if (ENVOY_SANITIZE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(ENVOY_TCMALLOC OFF CACHE BOOL "" FORCE)
endif()

option(ENVOY_TCMALLOC "build with tcmalloc" ON)
if (ENVOY_TCMALLOC)
  add_definitions(-DTCMALLOC)
endif()

option(ENVOY_STRIP "strip symbols from binaries" OFF)

option(ENVOY_USE_CCACHE "build with ccache" OFF)
if (ENVOY_USE_CCACHE)
  find_program(CCACHE_FOUND ccache)
  if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
  endif()
endif()

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${ENVOY_COTIRE_MODULE_DIR}")
include(cotire)
