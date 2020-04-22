#include "extensions/access_loggers/grpc/grpc_access_log_proto_descriptors.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

void validateProtoDescriptors() {
  const auto method = "envoy.service.accesslog.v2.AccessLogService.StreamAccessLogs";

  RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                 "");
};
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
