add_library(envoy-server OBJECT
  config/http/buffer.cc
  config/http/dynamo.cc
  config/http/fault.cc
  config/http/grpc_http1_bridge.cc
  config/http/ratelimit.cc
  config/http/router.cc
  config/network/client_ssl_auth.cc
  config/network/echo.cc
  config/network/http_connection_manager.cc
  config/network/mongo_proxy.cc
  config/network/ratelimit.cc
  config/network/redis_proxy.cc
  config/network/tcp_proxy.cc
  configuration_impl.cc
  connection_handler_impl.cc
  drain_manager_impl.cc
  guarddog_impl.cc
  http/admin.cc
  http/health_check.cc
  options_impl.cc
  server.cc
  watchdog_impl.cc
  worker.cc)

include_directories(SYSTEM ${ENVOY_TCLAP_INCLUDE_DIR})
include_directories(SYSTEM ${ENVOY_HTTP_PARSER_INCLUDE_DIR})
include_directories(${ENVOY_NGHTTP2_INCLUDE_DIR})
include_directories(SYSTEM ${ENVOY_OPENSSL_INCLUDE_DIR})
include_directories(SYSTEM ${ENVOY_LIGHTSTEP_TRACER_INCLUDE_DIR})
include_directories(${ENVOY_LIBEVENT_INCLUDE_DIR})

set_target_properties(envoy-server PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT
                      "../precompiled/precompiled.h")
cotire(envoy-server)

# Needed due to generated proto headers. There is probably a way to have this only depend on the
# generation of the header but I don't feel like figuring that out right now.
add_dependencies(envoy-server envoy-common)
