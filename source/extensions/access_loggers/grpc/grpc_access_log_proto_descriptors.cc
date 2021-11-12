#include "source/extensions/access_loggers/grpc/grpc_access_log_proto_descriptors.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

void validateProtoDescriptors() {
  const auto method = "envoy.service.accesslog.v3.AccessLogService.StreamAccessLogs";

  RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                 "");
};
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
