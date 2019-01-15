#pragma once

#include "envoy/server/resource_monitor_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class ResourceMonitorFactoryContextImpl : public ResourceMonitorFactoryContext {
public:
  ResourceMonitorFactoryContextImpl(Event::Dispatcher& dispatcher,
                                    Filesystem::Instance& file_system)
      : dispatcher_(dispatcher), file_system_(file_system) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  Filesystem::Instance& fileSystem() override { return file_system_; }

private:
  Event::Dispatcher& dispatcher_;
  Filesystem::Instance& file_system_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
