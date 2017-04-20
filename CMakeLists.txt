cmake_minimum_required(VERSION 2.8)

project(envoy CXX)

include(common.cmake)
include(thirdparty.cmake)

# Setup build ID via the linker. Also, statically link GCC libraries to lock GCC version.
execute_process(COMMAND git -C ${CMAKE_SOURCE_DIR} rev-parse --sq HEAD OUTPUT_VARIABLE GIT_COMMIT WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set(CMAKE_EXE_LINKER_FLAGS "-Wl,--build-id=0x${GIT_COMMIT} -static-libstdc++ -static-libgcc ${ENVOY_EXE_EXTRA_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_BINARY_DIR}/source)
include_directories(${PROJECT_SOURCE_DIR}/source)
include_directories(${ENVOY_SPDLOG_INCLUDE_DIR})
include_directories(SYSTEM ${ENVOY_PROTOBUF_INCLUDE_DIR})

add_subdirectory(source)
add_subdirectory(test)

set(CHECK_FORMAT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_format.py)
add_custom_target(check_format ${CHECK_FORMAT_PATH} check ${CMAKE_SOURCE_DIR})
add_custom_target(fix_format ${CHECK_FORMAT_PATH} fix ${CMAKE_SOURCE_DIR})
