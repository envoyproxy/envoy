#include "common/grpc/google_grpc_context.h"

#include <atomic>

#include "common/common/assert.h"

#include "grpcpp/grpcpp.h"

namespace Envoy {
namespace Grpc {

std::atomic<uint64_t> GoogleGrpcContext::live_instances_{0};

GoogleGrpcContext::GoogleGrpcContext() {
  if (++live_instances_ == 1) {
    grpc_init();
  }
}

GoogleGrpcContext::~GoogleGrpcContext() {
  ASSERT(live_instances_ > 0);
  if (--live_instances_ == 0) {
    grpc_shutdown();
  }
}

} // namespace Grpc
} // namespace Envoy
