#pragma once

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

/**
 * Well-known network filter names.
 * NOTE: New filters should use the well known name: envoy.filters.network.name.
 */
class NetworkFilterNameValues {
public:
  // Client ssl auth filter
  const std::string ClientSslAuth = "envoy.client_ssl_auth";
  // Echo filter
  const std::string Echo = "envoy.echo";
  // HTTP connection manager filter
  const std::string HttpConnectionManager = "envoy.http_connection_manager";
  // Mongo proxy filter
  const std::string MongoProxy = "envoy.mongo_proxy";
  // Rate limit filter
  const std::string RateLimit = "envoy.ratelimit";
  // Redis proxy filter
  const std::string RedisProxy = "envoy.redis_proxy";
  // IP tagging filter
  const std::string TcpProxy = "envoy.tcp_proxy";
  // Authorization filter
  const std::string ExtAuthorization = "envoy.ext_authz";
  // Thrift proxy filter
  const std::string ThriftProxy = "envoy.filters.network.thrift_proxy";

  // Converts names from v1 to v2
  const Config::V1Converter v1_converter_;

  // NOTE: Do not add any new filters to this list. All future filters are v2 only.
  NetworkFilterNameValues()
      : v1_converter_({ClientSslAuth, Echo, HttpConnectionManager, MongoProxy, RateLimit,
                       RedisProxy, TcpProxy, ExtAuthorization}) {}
};

typedef ConstSingleton<NetworkFilterNameValues> NetworkFilterNames;

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
