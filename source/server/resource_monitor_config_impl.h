#pragma once

#include "envoy/server/resource_monitor_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class ResourceMonitorFactoryContextImpl : public ResourceMonitorFactoryContext {
public:
  ResourceMonitorFactoryContextImpl(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

private:
  Event::Dispatcher& dispatcher_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
