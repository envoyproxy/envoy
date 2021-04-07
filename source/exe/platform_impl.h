#pragma once

#include "envoy/server/platform.h"

namespace Envoy {

class PlatformImpl : public Server::Platform {
public:
  PlatformImpl();
  ~PlatformImpl() override;
  Thread::ThreadFactory& threadFactory() override { return *thread_factory_; }
  Filesystem::Instance& fileSystem() override { return *file_system_; }
  bool enableCoreDump() override;

private:
  Thread::ThreadFactoryPtr thread_factory_;
  Filesystem::InstancePtr file_system_;
};

} // namespace Envoy
