#include "extensions/filters/http/dynamic_forward_proxy/dynamic_forward_proxy_proto_descriptors.h"

#include "envoy/config/cluster/dynamic_forward_proxy/v2alpha/cluster.pb.h"

#include "common/common/fmt.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

// Reference the dummy proto to ensure that the proto file doesn't get stripped.
const envoy::config::cluster::dynamic_forward_proxy::v2alpha::ClusterDummy _cluster_dummy;

bool validateProtoDescriptors() {
  const auto message = "envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig";

  return Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(message) != nullptr;
};

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
