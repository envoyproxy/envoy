#pragma once

#include <string>

namespace Envoy {
namespace Json {

class Schema {
public:
  // Top Level Config Schemas
  static const std::string TOP_LEVEL_CONFIG_SCHEMA;

  // Access log Schema
  static const std::string ACCESS_LOG_SCHEMA;

  // Listener Schema
  static const std::string LISTENER_SCHEMA;
  static const std::string LDS_SCHEMA;
  static const std::string LDS_CONFIG_SCHEMA;

  // Network Filter Schemas
  static const std::string CLIENT_SSL_NETWORK_FILTER_SCHEMA;
  static const std::string HTTP_CONN_NETWORK_FILTER_SCHEMA;
  static const std::string MONGO_PROXY_NETWORK_FILTER_SCHEMA;
  static const std::string RATELIMIT_NETWORK_FILTER_SCHEMA;
  static const std::string REDIS_PROXY_NETWORK_FILTER_SCHEMA;
  static const std::string TCP_PROXY_NETWORK_FILTER_SCHEMA;

  // HTTP Connection Manager Schemas
  static const std::string ROUTE_CONFIGURATION_SCHEMA;
  static const std::string VIRTUAL_HOST_CONFIGURATION_SCHEMA;
  static const std::string ROUTE_ENTRY_CONFIGURATION_SCHEMA;
  static const std::string HTTP_RATE_LIMITS_CONFIGURATION_SCHEMA;
  static const std::string RDS_CONFIGURATION_SCHEMA;
  static const std::string HEADER_DATA_CONFIGURATION_SCHEMA;
  static const std::string QUERY_PARAMETER_CONFIGURATION_SCHEMA;

  // HTTP Filter Schemas
  static const std::string BUFFER_HTTP_FILTER_SCHEMA;
  static const std::string FAULT_HTTP_FILTER_SCHEMA;
  static const std::string GRPC_JSON_TRANSCODER_FILTER_SCHEMA;
  static const std::string HEALTH_CHECK_HTTP_FILTER_SCHEMA;
  static const std::string RATE_LIMIT_HTTP_FILTER_SCHEMA;
  static const std::string ROUTER_HTTP_FILTER_SCHEMA;
  static const std::string LUA_HTTP_FILTER_SCHEMA;
  static const std::string SQUASH_HTTP_FILTER_SCHEMA;

  // Cluster Schemas
  static const std::string CLUSTER_MANAGER_SCHEMA;
  static const std::string CLUSTER_HEALTH_CHECK_SCHEMA;
  static const std::string CLUSTER_SCHEMA;

  // Discovery Service Schemas
  static const std::string CDS_SCHEMA;
  static const std::string SDS_SCHEMA;

  // Redis Schemas
  static const std::string REDIS_CONN_POOL_SCHEMA;
};

} // namespace Json
} // namespace Envoy
