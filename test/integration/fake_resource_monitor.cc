#include "test/integration/fake_resource_monitor.h"

namespace Envoy {

FakeResourceMonitor::~FakeResourceMonitor() { factory_.onMonitorDestroyed(); }

void FakeResourceMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  Server::ResourceUsage usage;
  usage.resource_pressure_ = pressure_;
  callbacks.onSuccess(usage);
}

void FakeResourceMonitorFactory::onMonitorDestroyed() { monitor_ = nullptr; }

Server::ResourceMonitorPtr FakeResourceMonitorFactory::createResourceMonitor(
    const Protobuf::Message&, Server::Configuration::ResourceMonitorFactoryContext& context) {
  auto monitor = std::make_unique<FakeResourceMonitor>(context.mainThreadDispatcher(), *this);
  monitor_ = monitor.get();
  return monitor;
}

} // namespace Envoy
