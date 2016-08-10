add_executable(envoy hot_restart.cc main.cc $<TARGET_OBJECTS:envoy-server> $<TARGET_OBJECTS:envoy-common> ${ENVOY_EXE_EXTRA_OBJECTS})

if (ENVOY_TCMALLOC)
  target_link_libraries(envoy tcmalloc_and_profiler)
endif()

target_link_libraries(envoy event)
target_link_libraries(envoy event_openssl)
target_link_libraries(envoy event_pthreads)
target_link_libraries(envoy http_parser)
target_link_libraries(envoy jansson)
target_link_libraries(envoy ssl)
target_link_libraries(envoy crypto)
target_link_libraries(envoy nghttp2)
target_link_libraries(envoy protobuf)
target_link_libraries(envoy pthread)
target_link_libraries(envoy anl)
target_link_libraries(envoy rt)
target_link_libraries(envoy dl)

include_directories(SYSTEM ${ENVOY_OPENSSL_INCLUDE_DIR})

set_target_properties(envoy PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT
                      "../precompiled/precompiled.h")
cotire(envoy)
