set(gen_git_sha_target ${CMAKE_CURRENT_BINARY_DIR}/version_generated.cc)
add_custom_target(
  gen_git_sha ALL
  COMMAND ${PROJECT_SOURCE_DIR}/tools/gen_git_sha.sh ${CMAKE_SOURCE_DIR} ${gen_git_sha_target})

add_custom_command(
  OUTPUT ${gen_git_sha_target}
  DEPENDS gen_git_sha)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/ratelimit.pb.h ${CMAKE_CURRENT_BINARY_DIR}/generated/ratelimit.pb.cc
  COMMAND ${ENVOY_PROTOBUF_PROTOC} -I=${CMAKE_CURRENT_SOURCE_DIR}/ratelimit/
          --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/generated
          ${CMAKE_CURRENT_SOURCE_DIR}/ratelimit/ratelimit.proto
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ratelimit/ratelimit.proto
)

set_source_files_properties(generated/ratelimit.pb.cc PROPERTIES COMPILE_FLAGS -Wno-unused-parameter)

add_library(
  envoy-common OBJECT
  api/api_impl.cc
  access_log/access_log_manager.cc
  buffer/buffer_impl.cc
  common/base64.cc
  common/hex.cc
  common/logger.cc
  common/thread.cc
  common/utility.cc
  common/version.cc
  dynamo/dynamo_filter.cc
  dynamo/dynamo_request_parser.cc
  event/dispatcher_impl.cc
  event/event_impl_base.cc
  event/file_event_impl.cc
  event/libevent.cc
  event/signal_impl.cc
  event/timer_impl.cc
  filesystem/filesystem_impl.cc
  filesystem/watcher_impl.cc
  filter/auth/client_ssl.cc
  filter/echo.cc
  filter/ratelimit.cc
  filter/tcp_proxy.cc
  generated/ratelimit.pb.cc
  grpc/common.cc
  grpc/http1_bridge_filter.cc
  grpc/rpc_channel_impl.cc
  http/access_log/access_log_formatter.cc
  http/access_log/access_log_impl.cc
  http/async_client_impl.cc
  http/codec_client.cc
  http/codes.cc
  http/conn_manager_impl.cc
  http/conn_manager_utility.cc
  http/header_map_impl.cc
  http/message_impl.cc
  http/pooled_stream_encoder.cc
  http/http1/codec_impl.cc
  http/http1/conn_pool.cc
  http/http2/codec_impl.cc
  http/http2/conn_pool.cc
  http/filter/buffer_filter.cc
  http/filter/ratelimit.cc
  http/user_agent.cc
  http/utility.cc
  json/json_loader.cc
  memory/stats.cc
  mongo/bson_impl.cc
  mongo/codec_impl.cc
  mongo/proxy.cc
  mongo/utility.cc
  network/connection_impl.cc
  network/dns_impl.cc
  network/filter_manager.cc
  network/listener_impl.cc
  network/listen_socket_impl.cc
  network/proxy_protocol.cc
  network/utility.cc
  profiler/profiler.cc
  ratelimit/ratelimit_impl.cc
  router/config_impl.cc
  router/retry_state_impl.cc
  router/router.cc
  router/shadow_writer_impl.cc
  runtime/runtime_impl.cc
  runtime/uuid_util.cc
  ssl/connection_impl.cc
  ssl/context_config_impl.cc
  ssl/context_impl.cc
  ssl/context_manager_impl.cc
  ssl/openssl.cc
  stats/stats_impl.cc
  stats/statsd.cc
  thread_local/thread_local_impl.cc
  tracing/http_tracer_impl.cc
  upstream/cluster_manager_impl.cc
  upstream/health_checker_impl.cc
  upstream/host_utility.cc
  upstream/load_balancer_impl.cc
  upstream/logical_dns_cluster.cc
  upstream/outlier_detection_impl.cc
  upstream/sds.cc
  upstream/upstream_impl.cc
  ${gen_git_sha_target})

include_directories(SYSTEM ${ENVOY_HTTP_PARSER_INCLUDE_DIR})
include_directories(${ENVOY_JANSSON_INCLUDE_DIR})

if (NOT ENVOY_SANITIZE)
  include_directories(${ENVOY_GPERFTOOLS_INCLUDE_DIR})
endif()

include_directories(${ENVOY_LIBEVENT_INCLUDE_DIR})
include_directories(${ENVOY_NGHTTP2_INCLUDE_DIR})
include_directories(SYSTEM ${ENVOY_OPENSSL_INCLUDE_DIR})
include_directories(SYSTEM ${ENVOY_LIGHTSTEP_TRACER_INCLUDE_DIR})

set_target_properties(envoy-common PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT
                      "../precompiled/precompiled.h")
cotire(envoy-common)
