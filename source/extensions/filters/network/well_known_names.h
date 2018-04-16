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
  const std::string CLIENT_SSL_AUTH = "envoy.client_ssl_auth";
  // Echo filter
  const std::string ECHO = "envoy.echo";
  // HTTP connection manager filter
  const std::string HTTP_CONNECTION_MANAGER = "envoy.http_connection_manager";
  // Mongo proxy filter
  const std::string MONGO_PROXY = "envoy.mongo_proxy";
  // Rate limit filter
  const std::string RATE_LIMIT = "envoy.ratelimit";
  // Redis proxy filter
  const std::string REDIS_PROXY = "envoy.redis_proxy";
  // IP tagging filter
  const std::string TCP_PROXY = "envoy.tcp_proxy";
  // Authorization filter
  const std::string EXT_AUTHORIZATION = "envoy.ext_authz";

  // Converts names from v1 to v2
  const Config::V1Converter v1_converter_;

  // NOTE: Do not add any new filters to this list. All future filters are v2 only.
  NetworkFilterNameValues()
      : v1_converter_({CLIENT_SSL_AUTH, ECHO, HTTP_CONNECTION_MANAGER, MONGO_PROXY, RATE_LIMIT,
                       REDIS_PROXY, TCP_PROXY, EXT_AUTHORIZATION}) {}
};

typedef ConstSingleton<NetworkFilterNameValues> NetworkFilterNames;

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
