#include "common/grpc/google_grpc_context.h"

#include <atomic>

#include "common/common/assert.h"
#include "common/common/lock_guard.h"
#include "common/common/macros.h"
#include "common/common/thread.h"

#ifdef ENVOY_GOOGLE_GRPC
#include "grpcpp/grpcpp.h"
#endif

namespace Envoy {
namespace Grpc {

GoogleGrpcContext::GoogleGrpcContext() : instance_tracker_(instanceTracker()) {
#ifdef ENVOY_GOOGLE_GRPC
  Thread::LockGuard lock(instance_tracker_.mutex_);
  if (++instance_tracker_.live_instances_ == 1) {
    grpc_init();
  }
#endif
}

GoogleGrpcContext::~GoogleGrpcContext() {
#ifdef ENVOY_GOOGLE_GRPC
  // Per https://github.com/grpc/grpc/issues/20303 it is OK to call
  // grpc_shutdown_blocking() as long as no one can concurrently call
  // grpc_init(). We use check_format.py to ensure that this file contains the
  // only callers to grpc_init(), and the mutex to then make that guarantee
  // across users of this class.
  Thread::LockGuard lock(instance_tracker_.mutex_);
  ASSERT(instance_tracker_.live_instances_ > 0);
  if (--instance_tracker_.live_instances_ == 0) {
    grpc_shutdown_blocking(); // Waiting for quiescence avoids non-determinism in tests.
  }
#endif
}

GoogleGrpcContext::InstanceTracker& GoogleGrpcContext::instanceTracker() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(InstanceTracker);
}

} // namespace Grpc
} // namespace Envoy
