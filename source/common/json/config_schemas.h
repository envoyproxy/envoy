#pragma once

namespace Json {

class Schema {
public:
  // Listener Schemas
  static const std::string LISTENER_SCHEMA;

  // Network Filter Schemas
  static const std::string CLIENT_SSL_SCHEMA;
  static const std::string MONGO_PROXY_SCHEMA;
  static const std::string RATELIMIT_SCHEMA;
  static const std::string REDIS_PROXY_SCHEMA;
  static const std::string TCP_PROXY_SCHEMA;
};

} // Json
