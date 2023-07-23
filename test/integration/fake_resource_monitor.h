#pragma once

#include "envoy/server/resource_monitor.h"
#include "envoy/server/resource_monitor_config.h"

#include "test/common/config/dummy_config.pb.h"

namespace Envoy {

class FakeResourceMonitorFactory;

class FakeResourceMonitor : public Server::ResourceMonitor {
public:
  FakeResourceMonitor(Event::Dispatcher& dispatcher, FakeResourceMonitorFactory& factory)
      : dispatcher_(dispatcher), factory_(factory) {}
  // Server::ResourceMonitor
  ~FakeResourceMonitor() override;
  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;

  void setResourcePressure(double pressure) {
    dispatcher_.post([this, pressure] { pressure_ = pressure; });
  }

private:
  Event::Dispatcher& dispatcher_;
  FakeResourceMonitorFactory& factory_;
  double pressure_{0.0};
};

class FakeResourceMonitorFactory : public Server::Configuration::ResourceMonitorFactory {
public:
  // Server::Configuration::ResourceMonitorFactory
  Server::ResourceMonitorPtr
  createResourceMonitor(const Protobuf::Message& config,
                        Server::Configuration::ResourceMonitorFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::common::config::DummyConfig>();
  }
  std::string name() const override {
    return "envoy.resource_monitors.testonly.fake_resource_monitor";
  }

  FakeResourceMonitor* monitor() const { return monitor_; }
  void onMonitorDestroyed();

private:
  FakeResourceMonitor* monitor_{nullptr};
};

} // namespace Envoy
