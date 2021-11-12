#include "source/extensions/stat_sinks/metrics_service/grpc_metrics_proto_descriptors.h"

#include "envoy/config/metrics/v3/metrics_service.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/api_version.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

void validateProtoDescriptors() {
  const auto method = "envoy.service.metrics.v3.MetricsService.StreamMetrics";

  RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                 "");

  const auto config = "envoy.config.metrics.v3.MetricsServiceConfig";

  // Keeping this as an ASSERT because ApiTypeOracle::getEarlierVersionDescriptor also has an
  // ASSERT.
  ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(config) != nullptr, "");
};
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
