#include "extensions/filters/http/dynamic_forward_proxy/dynamic_forward_proxy_proto_descriptors.h"

#include "common/common/fmt.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

bool validateProtoDescriptors() {
  const auto message = "envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig";

  return Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(message) != nullptr;
};

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
