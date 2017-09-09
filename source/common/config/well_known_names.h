#pragma once

#include <string>

#include "common/common/singleton.h"

namespace Envoy {
namespace Config {

/**
 * Well-known network filter names.
 */
class NetworkFilterNameValues {
public:
  // Client ssl auth filter
  const std::string CLIENT_SSL_AUTH = "client_ssl_auth";
  // Echo filter
  const std::string ECHO = "echo";
  // HTTP connection manager filter
  const std::string HTTP_CONNECTION_MANAGER = "http_connection_manager";
  // Mongo proxy filter
  const std::string MONGO_PROXY = "mongo_proxy";
  // Rate limit filter
  const std::string RATE_LIMIT = "ratelimit";
  // Redis proxy filter
  const std::string REDIS_PROXY = "redis_proxy";
  // IP tagging filter
  const std::string TCP_PROXY = "tcp_proxy";
};

typedef ConstSingleton<NetworkFilterNameValues> NetworkFilterNames;

/**
 * Well-known http filter names.
 */
class HttpFilterNameValues {
public:
  // Buffer filter
  const std::string BUFFER = "buffer";
  // Dynamo filter
  const std::string DYNAMO = "http_dynamo_filter";
  // Fault filter
  const std::string FAULT = "fault";
  // GRPC http1 bridge filter
  const std::string GRPC_HTTP1_BRIDGE = "grpc_http1_bridge";
  // GRPC json transcoder filter
  const std::string GRPC_JSON_TRANSCODER = "grpc_json_transcoder";
  // GRPC web filter
  const std::string GRPC_WEB = "grpc_web";
  // IP tagging filter
  const std::string IP_TAGGING = "ip_tagging";
  // Rate limit filter
  const std::string RATE_LIMIT = "rate_limit";
  // Router filter
  const std::string ROUTER = "router";
};

typedef ConstSingleton<HttpFilterNameValues> HttpFilterNames;

/**
 * Well-known access log names.
 */
class HttpTracerNameValues {
public:
  // Lightstep tracer
  const std::string LIGHTSTEP = "envoy.lightstep";
  // Zipkin tracer
  const std::string ZIPKIN = "envoy.zipkin";
};

typedef ConstSingleton<HttpTracerNameValues> HttpTracerNames;

/**
 * Well-known stats sink names.
 */
class StatsSinkNameValues {
public:
  // Statsd sink
  const std::string STATSD = "envoy.statsd";
};

typedef ConstSingleton<StatsSinkNameValues> StatsSinkNames;

/**
 * Well-known access log names.
 */
class AccessLogNameValues {
public:
  // File access log
  const std::string FILE = "envoy.file_access_log";
};

typedef ConstSingleton<AccessLogNameValues> AccessLogNames;

} // namespace Config
} // namespace Envoy