#include "extensions/stat_sinks/metrics_service/grpc_metrics_proto_descriptors.h"

#include "envoy/service/metrics/v2/metrics_service.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

void validateProtoDescriptors() {
  const auto method = "envoy.service.metrics.v2.MetricsService.StreamMetrics";

  RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                 "");
};
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
