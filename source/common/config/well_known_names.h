#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Config {

/**
 * Well-known address resolver names.
 */
class AddressResolverNameValues {
public:
  // Basic IP resolver
  const std::string IP = "envoy.ip";
};

using AddressResolverNames = ConstSingleton<AddressResolverNameValues>;

/**
 * Well-known metadata filter namespaces.
 */
class MetadataFilterValues {
public:
  // Filter namespace for built-in load balancer.
  const std::string ENVOY_LB = "envoy.lb";
  // Filter namespace for built-in transport socket match in cluster.
  const std::string ENVOY_TRANSPORT_SOCKET_MATCH = "envoy.transport_socket_match";
};

using MetadataFilters = ConstSingleton<MetadataFilterValues>;

/**
 * Keys for MetadataFilterValues::ENVOY_LB metadata.
 */
class MetadataEnvoyLbKeyValues {
public:
  // Key in envoy.lb filter namespace for endpoint canary bool value.
  const std::string CANARY = "canary";
};

using MetadataEnvoyLbKeys = ConstSingleton<MetadataEnvoyLbKeyValues>;

/**
 * Well known tags values and a mapping from these names to the regexes they
 * represent. Note: when names are added to the list, they also must be added to
 * the regex map by adding an entry in the getRegexMapping function.
 */
class TagNameValues {
public:
  TagNameValues();

  /**
   * Represents a tag extraction. This structure may be extended to
   * allow for an faster pattern-matching engine to be used as an
   * alternative to regexes, on an individual tag basis. Some of the
   * tags, such as "_rq_(\\d)xx$", will probably stay as regexes.
   */
  struct Descriptor {
    Descriptor(const std::string& name, const std::string& regex, const std::string& substr = "")
        : name_(name), regex_(regex), substr_(substr) {}
    const std::string name_;
    const std::string regex_;
    const std::string substr_;
  };

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
  const std::string DYNAMO_TABLE = "envoy.dynamo_table";
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
  // Route config name for RDS updates
  const std::string RDS_ROUTE_CONFIG = "envoy.rds_route_config";
  // Listener manager worker id
  const std::string WORKER_ID = "envoy.worker_id";

  // Mapping from the names above to their respective regex strings.
  const std::vector<std::pair<std::string, std::string>> name_regex_pairs_;

  // Returns the list of descriptors.
  const std::vector<Descriptor>& descriptorVec() const { return descriptor_vec_; }

private:
  void addRegex(const std::string& name, const std::string& regex, const std::string& substr = "");

  // Collection of tag descriptors.
  std::vector<Descriptor> descriptor_vec_;
};

using TagNames = ConstSingleton<TagNameValues>;

} // namespace Config
} // namespace Envoy
