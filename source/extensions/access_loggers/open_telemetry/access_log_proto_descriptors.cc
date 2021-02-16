#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/protobuf/protobuf.h"

#include "extensions/access_loggers/open_telemetry/access_log_proto_descriptors.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

void validateProtoDescriptors() {
  const auto method = "opentelemetry.proto.collector.logs.v1.LogsService.Export";

  RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                 "");
};
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
