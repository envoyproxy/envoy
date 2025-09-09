#pragma once

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/server/options_impl_base.h"

namespace Envoy {

// Process-wide lifecycle events for global state for Envoy Mobile. There should only ever be a
// singleton of this class.
class MobileProcessWide {
public:
  explicit MobileProcessWide(const OptionsImplBase& options);
  ~MobileProcessWide();

private:
  Thread::MutexBasicLockable log_lock_;
  std::unique_ptr<Logger::Context> logging_context_;
};

} // namespace Envoy
