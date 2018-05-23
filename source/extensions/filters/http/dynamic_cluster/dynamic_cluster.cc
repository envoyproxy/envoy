#include "extensions/filters/http/dynamic_cluster/dynamic_cluster.h"

#include <arpa/inet.h>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/config/cds_json.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicCluster {
// Implements StreamDecoderFilter.

Http::FilterHeadersStatus DynamicCluster::decodeHeaders(Http::HeaderMap& headers, bool) {

  const Http::LowerCaseString ClusterName{"x-envoy-cluster"};
  const Http::LowerCaseString ClusterHosts{"x-envoy-cluster-hosts"};
  const std::string cluster_name = headers.get(ClusterName)->value().c_str();
  const std::string cluster_hosts = headers.get(ClusterHosts)->value().c_str();
  std::istringstream ss(cluster_hosts);
  std::string host, port;
  std::getline(ss, host, ':');
  std::getline(ss, port, ':');

  // TODO: Make this part of filter config - possibly multiple templates for each type http1, http2
  // and select the cluster template based on request header.
  std::string cluster_template_yaml = fmt::sprintf(R"EOF(
    name: %s
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    common_http_protocol_options: {idle_timeout: 120s} 
    http2_protocol_options: {} 
    hosts:
      socket_address:
        address: %s
        port_value: %s
  )EOF",
                                                   cluster_name, host, port);

  envoy::api::v2::Cluster cluster;
  MessageUtil::loadFromYaml(cluster_template_yaml, cluster);
  cm_.addOrUpdateCluster(cluster, "1");
  return Http::FilterHeadersStatus::Continue;
}

} // namespace DynamicCluster
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
