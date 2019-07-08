#pragma once

#include "common/common/thread_impl.h"
#include "common/filesystem/filesystem_impl.h"

namespace Envoy {

class PlatformImpl {
public:
  Thread::ThreadFactory& threadFactory() { return thread_factory_; }
  Filesystem::Instance& fileSystem() { return file_system_; }

private:
  Thread::ThreadFactoryImplPosix thread_factory_;
  Filesystem::InstanceImplPosix file_system_;
};

} // namespace Envoy
