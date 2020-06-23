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
  const std::string ClientSslAuth = "envoy.filters.network.client_ssl_auth";
  // Echo filter
  const std::string Echo = "envoy.filters.network.echo";
  // Direct response filter
  const std::string DirectResponse = "envoy.filters.network.direct_response";
  // RocketMQ proxy filter
  const std::string RocketmqProxy = "envoy.filters.network.rocketmq_proxy";
  // Dubbo proxy filter
  const std::string DubboProxy = "envoy.filters.network.dubbo_proxy";
  // HTTP connection manager filter
  const std::string HttpConnectionManager = "envoy.filters.network.http_connection_manager";
  // Local rate limit filter
  const std::string LocalRateLimit = "envoy.filters.network.local_ratelimit";
  // Mongo proxy filter
  const std::string MongoProxy = "envoy.filters.network.mongo_proxy";
  // MySQL proxy filter
  const std::string MySQLProxy = "envoy.filters.network.mysql_proxy";
  // Postgres proxy filter
  const std::string Postgres = "envoy.filters.network.postgres_proxy";
  // Rate limit filter
  const std::string RateLimit = "envoy.filters.network.ratelimit";
  // Redis proxy filter
  const std::string RedisProxy = "envoy.filters.network.redis_proxy";
  // TCP proxy filter
  const std::string TcpProxy = "envoy.filters.network.tcp_proxy";
  // Authorization filter
  const std::string ExtAuthorization = "envoy.filters.network.ext_authz";
  // Kafka Broker filter
  const std::string KafkaBroker = "envoy.filters.network.kafka_broker";
  // Thrift proxy filter
  const std::string ThriftProxy = "envoy.filters.network.thrift_proxy";
  // Role based access control filter
  const std::string Rbac = "envoy.filters.network.rbac";
  // SNI Cluster filter
  const std::string SniCluster = "envoy.filters.network.sni_cluster";
  // SNI Dynamic forward proxy filter
  const std::string SniDynamicForwardProxy = "envoy.filters.network.sni_dynamic_forward_proxy";
  // ZooKeeper proxy filter
  const std::string ZooKeeperProxy = "envoy.filters.network.zookeeper_proxy";
};

using NetworkFilterNames = ConstSingleton<NetworkFilterNameValues>;

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
