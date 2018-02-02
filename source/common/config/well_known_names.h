#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/fmt.h"
#include "common/singleton/const_singleton.h"

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
 * Well-known listener filter names.
 */
class ListenerFilterNameValues {
public:
  // Original destination listener filter
  const std::string ORIGINAL_DST = "envoy.listener.original_dst";
  // Proxy Protocol listener filter
  const std::string PROXY_PROTOCOL = "envoy.listener.proxy_protocol";
};

typedef ConstSingleton<ListenerFilterNameValues> ListenerFilterNames;

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
 * Well-known address resolver names.
 */
class AddressResolverNameValues {
public:
  // Basic IP resolver
  const std::string IP = "envoy.ip";
};

typedef ConstSingleton<AddressResolverNameValues> AddressResolverNames;

/**
 * Well-known http filter names.
 */
class HttpFilterNameValues {
public:
  // Buffer filter
  const std::string BUFFER = "envoy.buffer";
  // CORS filter
  const std::string CORS = "envoy.cors";
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
  // Health checking filter
  const std::string HEALTH_CHECK = "envoy.health_check";
  // Lua filter
  const std::string LUA = "envoy.lua";
  // Squash filter
  const std::string SQUASH = "envoy.squash";

  // Converts names from v1 to v2
  const V1Converter v1_converter_;

  HttpFilterNameValues()
      : v1_converter_({BUFFER, CORS, DYNAMO, FAULT, GRPC_HTTP1_BRIDGE, GRPC_JSON_TRANSCODER,
                       GRPC_WEB, HEALTH_CHECK, IP_TAGGING, RATE_LIMIT, ROUTER, LUA}) {}
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
  // DogStatsD compatible stastsd sink
  const std::string DOG_STATSD = "envoy.dog_statsd";
  // MetricsService sink
  const std::string METRICS_SERVICE = "envoy.metrics_service";
};

typedef ConstSingleton<StatsSinkNameValues> StatsSinkNames;

/**
 * Well-known access log names.
 */
class AccessLogNameValues {
public:
  // File access log
  const std::string FILE = "envoy.file_access_log";
  // HTTP gRPC access log
  const std::string HTTP_GRPC = "envoy.http_grpc_access_log";
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

/**
 * Well known tags values and a mapping from these names to the regexes they
 * represent. Note: when names are added to the list, they also must be added to
 * the regex map by adding an entry in the getRegexMapping function.
 */
class TagNameValues {
public:
  // Cluster name tag
  const std::string CLUSTER_NAME = "envoy.cluster_name";
  // Listener port tag
  const std::string LISTENER_ADDRESS = "envoy.listener_address";
  // Stats prefix for HttpConnectionManager
  const std::string HTTP_CONN_MANAGER_PREFIX = "envoy.http_conn_manager_prefix";
  // User agent for a connection
  const std::string HTTP_USER_AGENT = "envoy.http_user_agent";
  // SSL cipher for a connection
  const std::string SSL_CIPHER = "envoy.ssl_cipher";
  // SSL cipher suite
  const std::string SSL_CIPHER_SUITE = "cipher_suite";
  // Stats prefix for the Client SSL Auth network filter
  const std::string CLIENTSSL_PREFIX = "envoy.clientssl_prefix";
  // Stats prefix for the Mongo Proxy network filter
  const std::string MONGO_PREFIX = "envoy.mongo_prefix";
  // Request command for the Mongo Proxy network filter
  const std::string MONGO_CMD = "envoy.mongo_cmd";
  // Request collection for the Mongo Proxy network filter
  const std::string MONGO_COLLECTION = "envoy.mongo_collection";
  // Request callsite for the Mongo Proxy network filter
  const std::string MONGO_CALLSITE = "envoy.mongo_callsite";
  // Stats prefix for the Ratelimit network filter
  const std::string RATELIMIT_PREFIX = "envoy.ratelimit_prefix";
  // Stats prefix for the TCP Proxy network filter
  const std::string TCP_PREFIX = "envoy.tcp_prefix";
  // Downstream cluster for the Fault http filter
  const std::string FAULT_DOWNSTREAM_CLUSTER = "envoy.fault_downstream_cluster";
  // Operation name for the Dynamo http filter
  const std::string DYNAMO_OPERATION = "envoy.dynamo_operation";
  // Table name for the Dynamo http filter
  const std::string DYNAMO_TABLE = "envoy.dyanmo_table";
  // Partition ID for the Dynamo http filter
  const std::string DYNAMO_PARTITION_ID = "envoy.dynamo_partition_id";
  // Request service name GRPC Bridge http filter
  const std::string GRPC_BRIDGE_SERVICE = "envoy.grpc_bridge_service";
  // Request method name for the GRPC Bridge http filter
  const std::string GRPC_BRIDGE_METHOD = "envoy.grpc_bridge_method";
  // Request virtual host given by the Router http filter
  const std::string VIRTUAL_HOST = "envoy.virtual_host";
  // Request virtual cluster given by the Router http filter
  const std::string VIRTUAL_CLUSTER = "envoy.virtual_cluster";
  // Request response code
  const std::string RESPONSE_CODE = "envoy.response_code";
  // Request response code class
  const std::string RESPONSE_CODE_CLASS = "envoy.response_code_class";

  // Mapping from the names above to their respective regex strings.
  const std::vector<std::pair<std::string, std::string>> name_regex_pairs_;

  // Constructor to fill map.
  TagNameValues() : name_regex_pairs_(getRegexMapping()) {}

private:
  // Creates a regex mapping for all tag names.
  std::vector<std::pair<std::string, std::string>> getRegexMapping();
};

typedef ConstSingleton<TagNameValues> TagNames;

class TransportSocketNameValues {
public:
  const std::string RAW_BUFFER = "raw_buffer";
  const std::string SSL = "ssl";
};

typedef ConstSingleton<TransportSocketNameValues> TransportSocketNames;

} // namespace Config
} // namespace Envoy
