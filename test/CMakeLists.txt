set(CMAKE_EXE_LINKER_FLAGS "${ENVOY_TEST_EXTRA_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")

# TODO: Generic function for building protos
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/helloworld.pb.h ${CMAKE_CURRENT_BINARY_DIR}/generated/helloworld.pb.cc
  COMMAND ${ENVOY_PROTOBUF_PROTOC} -I=${CMAKE_CURRENT_SOURCE_DIR}/proto/
          --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/generated
          ${CMAKE_CURRENT_SOURCE_DIR}/proto/helloworld.proto
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/helloworld.proto
)

set_source_files_properties(generated/helloworld.pb.cc PROPERTIES COMPILE_FLAGS -Wno-unused-parameter)

include_directories(${ENVOY_GMOCK_INCLUDE_DIR})
include_directories(${ENVOY_GTEST_INCLUDE_DIR})
include_directories(SYSTEM ${ENVOY_HTTP_PARSER_INCLUDE_DIR})
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})
include_directories(SYSTEM ${ENVOY_OPENSSL_INCLUDE_DIR})
include_directories(${ENVOY_NGHTTP2_INCLUDE_DIR})
include_directories(SYSTEM ${ENVOY_LIGHTSTEP_TRACER_INCLUDE_DIR})

add_executable(envoy-test
  $<TARGET_OBJECTS:envoy-server>
  $<TARGET_OBJECTS:envoy-common>
  ${ENVOY_TEST_EXTRA_OBJECTS}
  common/access_log/access_log_manager_impl_test.cc
  common/api/api_impl_test.cc
  common/common/hex_test.cc
  common/common/optional_test.cc
  common/common/utility_test.cc
  common/event/file_event_impl_test.cc
  common/filesystem/filesystem_impl_test.cc
  common/filesystem/watcher_impl_test.cc
  common/grpc/common_test.cc
  common/grpc/http1_bridge_filter_test.cc
  common/grpc/rpc_channel_impl_test.cc
  common/http/access_log/access_log_formatter_test.cc
  common/http/access_log/access_log_impl_test.cc
  common/http/async_client_impl_test.cc
  common/http/codec_client_test.cc
  common/http/codes_test.cc
  common/http/common.cc
  common/http/conn_manager_impl_test.cc
  common/http/conn_manager_utility_test.cc
  common/http/filter/buffer_filter_test.cc
  common/http/filter/ratelimit_test.cc
  common/http/header_map_impl_test.cc
  common/http/http1/codec_impl_test.cc
  common/http/http1/conn_pool_test.cc
  common/http/http2/codec_impl_test.cc
  common/http/http2/conn_pool_test.cc
  common/http/user_agent_test.cc
  common/http/utility_test.cc
  common/filter/auth/client_ssl_test.cc
  common/filter/ratelimit_test.cc
  common/filter/tcp_proxy_test.cc
  common/dynamo/dynamo_filter_test.cc
  common/dynamo/dynamo_request_parser_test.cc
  common/json/json_loader_test.cc
  common/mongo/bson_impl_test.cc
  common/mongo/codec_impl_test.cc
  common/mongo/proxy_test.cc
  common/mongo/utility_test.cc
  common/network/connection_impl_test.cc
  common/network/dns_impl_test.cc
  common/network/filter_manager_test.cc
  common/network/listen_socket_impl_test.cc
  common/network/proxy_protocol_test.cc
  common/network/utility_test.cc
  common/ratelimit/ratelimit_impl_test.cc
  common/router/config_impl_test.cc
  common/router/retry_state_impl_test.cc
  common/router/router_test.cc
  common/router/shadow_writer_impl_test.cc
  common/runtime/runtime_impl_test.cc
  common/runtime/uuid_util_test.cc
  common/ssl/connection_impl_test.cc
  common/ssl/context_impl_test.cc
  common/stats/statsd_test.cc
  common/tracing/http_tracer_impl_test.cc
  common/upstream/cluster_manager_impl_test.cc
  common/upstream/health_checker_impl_test.cc
  common/upstream/host_utility_test.cc
  common/upstream/load_balancer_impl_test.cc
  common/upstream/load_balancer_simulation_test.cc
  common/upstream/logical_dns_cluster_test.cc
  common/upstream/outlier_detection_impl_test.cc
  common/upstream/resource_manager_impl_test.cc
  common/upstream/sds_test.cc
  common/upstream/upstream_impl_test.cc
  example_configs_test.cc
  generated/helloworld.pb.cc
  integration/fake_upstream.cc
  integration/http2_integration_test.cc
  integration/http2_upstream_integration_test.cc
  integration/integration.cc
  integration/integration_test.cc
  integration/integration_admin_test.cc
  integration/proxy_proto_integration_test.cc
  integration/ssl_integration_test.cc
  integration/uds_integration_test.cc
  integration/server.cc
  integration/utility.cc
  main.cc
  mocks/access_log/mocks.cc
  mocks/api/mocks.cc
  mocks/common.cc
  mocks/event/mocks.cc
  mocks/filesystem/mocks.cc
  mocks/grpc/mocks.cc
  mocks/http/mocks.cc
  mocks/network/mocks.cc
  mocks/ratelimit/mocks.cc
  mocks/router/mocks.cc
  mocks/runtime/mocks.cc
  mocks/server/mocks.cc
  mocks/ssl/mocks.cc
  mocks/stats/mocks.cc
  mocks/thread_local/mocks.cc
  mocks/tracing/mocks.cc
  mocks/upstream/mocks.cc
  server/config/network/http_connection_manager_test.cc
  server/drain_manager_impl_test.cc
  server/http/admin_test.cc
  server/http/health_check_test.cc
  server/options_impl_test.cc
  test_common/printers.cc
  test_common/utility.cc)

if (ENVOY_TCMALLOC)
  target_link_libraries(envoy-test tcmalloc_and_profiler)
endif()

target_link_libraries(envoy-test event)
target_link_libraries(envoy-test event_openssl)
target_link_libraries(envoy-test event_pthreads)
target_link_libraries(envoy-test http_parser)
target_link_libraries(envoy-test jansson)
target_link_libraries(envoy-test ssl)
target_link_libraries(envoy-test crypto)
target_link_libraries(envoy-test nghttp2)
target_link_libraries(envoy-test lightstep_core_cxx11)
target_link_libraries(envoy-test protobuf)
target_link_libraries(envoy-test gmock)
target_link_libraries(envoy-test pthread)
target_link_libraries(envoy-test anl)
target_link_libraries(envoy-test dl)

add_custom_target(
  envoy.generate_example_configs configs/configgen.sh generated/configs ${PROJECT_BINARY_DIR}
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_custom_target(
  ${PROJECT_NAME}.check ${PROJECT_SOURCE_DIR}/test/run_envoy_tests.sh ${PROJECT_SOURCE_DIR}
                        ${PROJECT_BINARY_DIR}
  DEPENDS envoy envoy-test envoy.generate_example_configs
)

if (ENVOY_CODE_COVERAGE)
add_custom_target(
  ${PROJECT_NAME}.check-coverage
  DEPENDS envoy envoy-test envoy.generate_example_configs
  COMMAND ${PROJECT_SOURCE_DIR}/test/run_envoy_coverage.sh ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}
          ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR} ${ENVOY_GCOVR} ${ENVOY_GCOVR_EXTRA_ARGS}
)
endif()

set_target_properties(envoy-test PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT
                      "precompiled/precompiled_test.h")
cotire(envoy-test)
