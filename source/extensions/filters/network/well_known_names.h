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
  // Dubbo proxy filter
  const std::string DubboProxy = "envoy.filters.network.dubbo_proxy";
  // HTTP connection manager filter
  const std::string HttpConnectionManager = "envoy.http_connection_manager";
  // Mongo proxy filter
  const std::string MongoProxy = "envoy.mongo_proxy";
  // MySQL proxy filter
  const std::string MySQLProxy = "envoy.filters.network.mysql_proxy";
  // Rate limit filter
  const std::string RateLimit = "envoy.ratelimit";
  // Redis proxy filter
  const std::string RedisProxy = "envoy.redis_proxy";
  // TCP proxy filter
  const std::string TcpProxy = "envoy.tcp_proxy";
  // Authorization filter
  const std::string ExtAuthorization = "envoy.ext_authz";
  // Thrift proxy filter
  const std::string ThriftProxy = "envoy.filters.network.thrift_proxy";
  // Role based access control filter
  const std::string Rbac = "envoy.filters.network.rbac";
  // SNI Cluster filter
  const std::string SniCluster = "envoy.filters.network.sni_cluster";
  // ZooKeeper proxy filter
  const std::string ZooKeeperProxy = "envoy.filters.network.zookeeper_proxy";

  // Converts names from v1 to v2
  const Config::V1Converter v1_converter_;

  // NOTE: Do not add any new filters to this list. All future filters are v2 only.
  NetworkFilterNameValues()
      : v1_converter_({ClientSslAuth, Echo, HttpConnectionManager, MongoProxy, RateLimit,
                       RedisProxy, TcpProxy, ExtAuthorization}) {}
};

using NetworkFilterNames = ConstSingleton<NetworkFilterNameValues>;

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
