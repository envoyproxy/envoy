#include "extensions/stat_sinks/metrics_service/grpc_metrics_proto_descriptors.h"

#include "envoy/config/metrics/v2/metrics_service.pb.h"
#include "envoy/service/metrics/v2/metrics_service.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/api_version.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

void validateProtoDescriptors() {
  // https://github.com/envoyproxy/envoy/issues/9639
  const API_NO_BOOST(envoy::service::metrics::v2::StreamMetricsMessage) _dummy_service_v2;
  // https://github.com/envoyproxy/envoy/pull/9618 made it necessary to register the previous
  // version's config descriptor by calling ApiTypeOracle::getEarlierVersionDescriptor which has an
  // assertion for nullptr types.
  const API_NO_BOOST(envoy::config::metrics::v2::MetricsServiceConfig) _dummy_config_v2;

  const auto method = "envoy.service.metrics.v2.MetricsService.StreamMetrics";

  RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                 "");

  const auto config = "envoy.config.metrics.v2.MetricsServiceConfig";

  // Keeping this as an ASSERT because ApiTypeOracle::getEarlierVersionDescriptor also has an
  // ASSERT.
  ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(config) != nullptr, "");
};
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
