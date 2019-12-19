#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Factory {

/**
 * List of category names for all registered factories.
 */
class CategoryNames {
public:
  const std::string AccessLoggers = "access_loggers";
  const std::string AccessLoggerExtensionFilters = "access_logger.extension_filters";
  const std::string Clusters = "clusters";

  const std::string FiltersHttp = "filters.http";
  const std::string FiltersListener = "filters.listener";
  const std::string FiltersNetwork = "filters.network";
  const std::string FiltersUdpListener = "filters.udp_listener";
  const std::string FiltersUpstreamNetwork = "filters.upstream_network";

  const std::string GrpcCredentials = "grpc_credentials";
  const std::string HealthCheckers = "health_checkers";
  const std::string QuicClientCodec = "quic_client_codec";
  const std::string QuicServerCodec = "quic_server_codec";
  const std::string ResourceMonitors = "resource_monitors";
  const std::string RetryPriorities = "retry_priorities";
  const std::string RetryHostPredicates = "retry_host_predicates";
  const std::string Resolvers = "resolvers";
  const std::string StatsSinks = "stats_sinks";
  const std::string TlsKeyProviders = "tls.key_providers";
  const std::string Tracers = "tracers";
  const std::string TransportSocketsUpstream = "transport_sockets.upstream";
  const std::string TransportSocketsDownstream = "transport_sockets.downstream";
  const std::string TypedMetadata = "typed_metadata";
  const std::string UdpListeners = "udp_listeners";

  // Dubbo extension
  const std::string DubboProxyFilters = "dubbo_proxy.filters";
  const std::string DubboProxyProtocols = "dubbo_proxy.protocols";
  const std::string DubboProxyRouteMatchers = "dubbo_proxy.route_matchers";
  const std::string DubboProxySerializers = "dubbo_proxy.serializers";

  // Thrift extension
  const std::string ThriftProxyFilters = "thrift_proxy.filters";
  const std::string ThriftProxyProtocols = "thrift_proxy.protocols";
  const std::string ThriftProxyTransports = "thrift_proxy.transports";

  // WASM
  const std::string WasmNullVm = "wasm.null_vms";
};

using Categories = ConstSingleton<CategoryNames>;

} // namespace Factory
} // namespace Envoy
