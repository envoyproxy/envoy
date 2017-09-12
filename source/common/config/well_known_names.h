#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/singleton.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {

/**
 * Converts certain names from v1 to v2 by adding a prefix.
 */
class V1Converter {
public:
  /**
   * @param v2_names vector of all the v2 names that may be converted.
   */
  V1Converter(const std::vector<std::string>& v2_names) {
    const std::string prefix = "envoy.";
    for (const auto& name : v2_names) {
      // Ensure there are no misplaced names provided to this constructor.
      if (name.find(prefix) != 0) {
        throw EnvoyException(fmt::format(
            "Attempted to create a conversion for a v2 name that isn't prefixed by {}", prefix));
      }
      v1_to_v2_names_[name.substr(prefix.size())] = name;
    }
  }

  /**
   * Returns the v2 name for the provided v1 name. If it doesn't match one of the explicitly
   * provided names, it will return the same name.
   * @param v1_name the name to convert.
   */
  const std::string getV2Name(const std::string& v1_name) const {
    auto it = v1_to_v2_names_.find(v1_name);
    return (it == v1_to_v2_names_.end()) ? v1_name : it->second;
  }

private:
  std::unordered_map<std::string, std::string> v1_to_v2_names_;
};

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

  // Converts names from v1 to v2
  const V1Converter v1_converter_;

  NetworkFilterNameValues()
      : v1_converter_({CLIENT_SSL_AUTH, ECHO, HTTP_CONNECTION_MANAGER, MONGO_PROXY, RATE_LIMIT,
                       REDIS_PROXY, TCP_PROXY}) {}
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

  // Converts names from v1 to v2
  const V1Converter v1_converter_;

  HttpFilterNameValues()
      : v1_converter_({BUFFER, DYNAMO, FAULT, GRPC_HTTP1_BRIDGE, GRPC_JSON_TRANSCODER, GRPC_WEB,
                       IP_TAGGING, RATE_LIMIT, ROUTER}) {}
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