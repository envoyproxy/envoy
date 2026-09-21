#pragma once

#include <atomic>

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
  absl::StatusOr<Server::ResourceMonitorPtr>
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

class FakeRealtimeResourceMonitorFactory;

class FakeRealtimeResourceMonitor : public Server::RealtimeResourceMonitor {
public:
  explicit FakeRealtimeResourceMonitor(FakeRealtimeResourceMonitorFactory& factory)
      : factory_(factory) {}
  ~FakeRealtimeResourceMonitor() override;

  // Server::RealtimeResourceMonitor
  Server::ResourceUsage getResourceUsage() override {
    return {pressure_.load(std::memory_order_relaxed)};
  }
  void onLoadAccepted(absl::string_view load_shed_point_name) override {
    UNREFERENCED_PARAMETER(load_shed_point_name);
    load_accepted_count_.fetch_add(1, std::memory_order_relaxed);
  }

  void setRealtimePressure(double pressure) {
    pressure_.store(pressure, std::memory_order_relaxed);
  }
  uint64_t loadAcceptedCount() const {
    return load_accepted_count_.load(std::memory_order_relaxed);
  }

private:
  FakeRealtimeResourceMonitorFactory& factory_;
  std::atomic<double> pressure_{0.0};
  std::atomic<uint64_t> load_accepted_count_{0};
};

class FakeRealtimeResourceMonitorFactory
    : public Server::Configuration::RealtimeResourceMonitorFactory {
public:
  // `Server::Configuration::RealtimeResourceMonitorFactory`
  absl::StatusOr<Server::RealtimeResourceMonitorPtr> createRealtimeResourceMonitor(
      const Protobuf::Message& config,
      Server::Configuration::ResourceMonitorFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::DoubleValue>();
  }
  std::string name() const override {
    return "envoy.resource_monitors.testonly.fake_realtime_resource_monitor";
  }

  FakeRealtimeResourceMonitor* monitor() const { return monitor_.load(std::memory_order_relaxed); }
  void onMonitorDestroyed();

private:
  std::atomic<FakeRealtimeResourceMonitor*> monitor_{nullptr};
};

} // namespace Envoy
