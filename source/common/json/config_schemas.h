#pragma once

namespace Json {

class Schema {
public:
  // Listener Schemas
  static const std::string LISTENER_SCHEMA;

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

  // HTTP Filter Schemas
  static const std::string BUFFER_HTTP_FILTER_SCHEMA;
  static const std::string FAULT_HTTP_FILTER_SCHEMA;
  static const std::string DYNAMO_HTTP_FILTER_SCHEMA;
  static const std::string HEALTH_CHECK_HTTP_FILTER_SCHEMA;
  static const std::string RATE_LIMIT_HTTP_FILTER_SCHEMA;
  static const std::string ROUTER_HTTP_FILTER_SCHEMA;
};

} // Json
