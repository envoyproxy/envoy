#pragma once

#include "common/common/macros.h"
#include "common/common/thread_impl.h"

namespace Envoy {

class PlatformImpl {
public:
  Thread::ThreadFactory& threadFactory() { return thread_factory_; }

private:
  Thread::ThreadFactoryImplPosix thread_factory_;
};

} // namespace Envoy