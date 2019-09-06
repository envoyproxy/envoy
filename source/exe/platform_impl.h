#pragma once

#include "envoy/filesystem/filesystem.h"
#include "envoy/thread/thread.h"

namespace Envoy {

class PlatformImpl {
public:
  PlatformImpl();
  ~PlatformImpl();
  Thread::ThreadFactory& threadFactory() { return *thread_factory_; }
  Filesystem::Instance& fileSystem() { return *file_system_; }

private:
  std::unique_ptr<Thread::ThreadFactory> thread_factory_;
  std::unique_ptr<Filesystem::Instance> file_system_;
};

} // namespace Envoy
