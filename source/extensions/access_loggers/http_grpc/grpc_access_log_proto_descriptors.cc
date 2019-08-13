#include "extensions/access_loggers/http_grpc/grpc_access_log_proto_descriptors.h"

#include "envoy/service/accesslog/v2/als.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace HttpGrpc {

void validateProtoDescriptors() {
  const auto method = "envoy.service.accesslog.v2.AccessLogService.StreamAccessLogs";

  RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                 "");
};
} // namespace HttpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
