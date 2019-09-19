#include "common/grpc/google_grpc_context.h"

#include <atomic>

#include "common/common/assert.h"

#include "grpcpp/grpcpp.h"

namespace Envoy {
namespace Grpc {

std::atomic<uint64_t> GoogleGrpcContext::live_instances_{0};

GoogleGrpcContext::GoogleGrpcContext() {
  if (++live_instances_ == 1) {
#ifdef ENVOY_GOOGLE_GRPC
    grpc_init();
#endif
  }
}

GoogleGrpcContext::~GoogleGrpcContext() {
  ASSERT(live_instances_ > 0);
  if (--live_instances_ == 0) {
#ifdef ENVOY_GOOGLE_GRPC
    grpc_shutdown_blocking(); // Waiting for quiescence avoids non-determinism in tests.
#endif
  }
}

} // namespace Grpc
} // namespace Envoy
