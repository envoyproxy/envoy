#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "common/common/singleton.h"

namespace Envoy {
namespace Config {

/**
 * Well-known network filter names.
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

  NetworkFilterNameValues() {
    const std::vector<std::string> prefixed_v2_names = {CLIENT_SSL_AUTH, ECHO, HTTP_CONNECTION_MANAGER, 
      MONGO_PROXY, RATE_LIMIT, REDIS_PROXY, TCP_PROXY};

    static constexpr std::size_t envoy_prefix_length = 6;
    for (const auto &v2_name : prefixed_v2_names) {
      v1_to_v2_names_[v2_name.substr(envoy_prefix_length)] = v2_name;
    }
  }

  const std::string getV2Name(const std::string &v1_name) const {
    auto it = v1_to_v2_names_.find(v1_name);
    return it == (v1_to_v2_names_.end()) ? v1_name : it->second;
  }

  private:
  std::unordered_map<std::string, std::string> v1_to_v2_names_;
};

typedef ConstSingleton<NetworkFilterNameValues> NetworkFilterNames;

/**
 * Well-known http filter names.
 */
class HttpFilterNameValues {
public:
  // Buffer filter
  const std::string BUFFER = "envoy.buffer";
  // Dynamo filter
  const std::string DYNAMO = "envoy.http_dynamo_filter";
  // Fault filter
  const std::string FAULT = "envoy.fault";
  // GRPC http1 bridge filter
  const std::string GRPC_HTTP1_BRIDGE = "envoy.grpc_http1_bridge";
  // GRPC json transcoder filter
  const std::string GRPC_JSON_TRANSCODER = "envoy.grpc_json_transcoder";
  // GRPC web filter
  const std::string GRPC_WEB = "envoy.grpc_web";
  // IP tagging filter
  const std::string IP_TAGGING = "envoy.ip_tagging";
  // Rate limit filter
  const std::string RATE_LIMIT = "envoy.rate_limit";
  // Router filter
  const std::string ROUTER = "envoy.router";

  HttpFilterNameValues() {
    const std::vector<std::string> prefixed_v2_names = {BUFFER, DYNAMO, FAULT, 
      GRPC_HTTP1_BRIDGE, GRPC_JSON_TRANSCODER, GRPC_WEB, IP_TAGGING, RATE_LIMIT, ROUTER};

    static constexpr std::size_t envoy_prefix_length = 6;
    for (const auto &v2_name : prefixed_v2_names) {
      v1_to_v2_names_[v2_name.substr(envoy_prefix_length)] = v2_name;
    }
  }

  const std::string getV2Name(const std::string &v1_name) const {
    auto it = v1_to_v2_names_.find(v1_name);
    return (it == v1_to_v2_names_.end()) ? v1_name : it->second;
  }

private:
  std::unordered_map<std::string, std::string> v1_to_v2_names_;
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


/**
 * Well-known metadata filter namespaces.
 */
class MetadataFilterValues {
public:
  // Filter namespace for built-in load balancer.
  const std::string ENVOY_LB = "envoy.lb";
  // Filter namespace for built-in router opaque data.
  const std::string ENVOY_ROUTER = "envoy.router";
};

typedef ConstSingleton<MetadataFilterValues> MetadataFilters;

/**
 * Keys for MetadataFilterConstants::ENVOY_LB metadata.
 */
class MetadataEnvoyLbKeyValues {
public:
  // Key in envoy.lb filter namespace for endpoint canary bool value.
  const std::string CANARY = "canary";
};

typedef ConstSingleton<MetadataEnvoyLbKeyValues> MetadataEnvoyLbKeys;

} // namespace Config
} // namespace Envoy