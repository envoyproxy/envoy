#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_proto_descriptors.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/common/opentelemetry/sdk/metrics/constants.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

void validateProtoDescriptors() {
  const auto method = std::string(Envoy::Extensions::Common::OpenTelemetry::Sdk::Metrics::
                                      Constants::METRICS_SERVICE_EXPORT_METHOD);

  RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                 "");
};

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
