#pragma once

#include "common/common/thread.h"

namespace Envoy {
namespace Grpc {

// Captures global grpc initialization and shutdown. Note that grpc
// initialization starts several threads, so it is a little annoying to run them
// alongside unrelated tests, particularly if they are trying to track memory
// usage, or you are exploiting otherwise consistent run-to-run pointer values
// during debug.
//
// Instantiating this class makes it easy to ensure classes that depend on grpc
// libraries get them initialized.
class GoogleGrpcContext {
public:
  GoogleGrpcContext();
  ~GoogleGrpcContext();

private:
  struct InstanceTracker {
    Thread::MutexBasicLockable mutex_;
    uint64_t live_instances_ GUARDED_BY(mutex_) = 0;
  };

  static InstanceTracker& instanceTracker();

  InstanceTracker& instance_tracker_;
};

} // namespace Grpc
} // namespace Envoy
