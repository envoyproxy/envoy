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

Http::FilterHeadersStatus DynamicCluster::decodeHeaders(Http::HeaderMap&, bool) {
  // TODO: read these values from headers.
  std::string cluster_yaml = fmt::sprintf(R"EOF(
    name: %s
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    common_http_protocol_options: {idle_timeout: 120s} 
    http2_protocol_options: {} 
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 5555
  )EOF",
                                          "time");
  envoy::api::v2::Cluster cluster;
  MessageUtil::loadFromYaml(cluster_yaml, cluster);
  cm_.addOrUpdateCluster(cluster, "1");
  return Http::FilterHeadersStatus::Continue;
}

} // namespace DynamicCluster
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
