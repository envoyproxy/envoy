#include <optional>

#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/thread.h"
#include "source/extensions/resource_monitors/cpu_utilization/config.h"
#include "source/extensions/resource_monitors/cpu_utilization/linux_cpu_stats_reader.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/options.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {
namespace {

class TestResourcePressureCallbacks : public Server::ResourceUpdateCallbacks {
public:
  void onSuccess(const Server::ResourceUsage& usage) override {
    pressure_ = usage.resource_pressure_;
    has_success_ = true;
  }

  void onFailure(const absl::Status& error) override {
    error_ = error;
    has_error_ = true;
  }

  bool hasSuccess() const { return has_success_; }
  bool hasError() const { return has_error_; }
  double pressure() const { return pressure_.value_or(0.0); }

private:
  std::optional<double> pressure_;
  std::optional<absl::Status> error_;
  bool has_success_ = false;
  bool has_error_ = false;
};

TEST(CpuUtilizationMonitorFactoryTest, CreateMonitorDefault) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cpu_utilization");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  EXPECT_EQ(config.mode(),
            envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::HOST);
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  testing::NiceMock<Runtime::MockLoader> runtime;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor(), runtime);
  auto monitor_or_error = factory->createResourceMonitor(config, context);
  EXPECT_TRUE(monitor_or_error.ok());
  EXPECT_NE(monitor_or_error.value(), nullptr);
}

TEST(CpuUtilizationMonitorFactoryTest, CreateContainerCPUMonitor) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cpu_utilization");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  EXPECT_EQ(
      config.mode(),
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  testing::NiceMock<Runtime::MockLoader> runtime;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor(), runtime);

  auto monitor_or_error = factory->createResourceMonitor(config, context);
#if defined(__linux__)
  if (!monitor_or_error.ok()) {
    ASSERT_THAT(std::string(monitor_or_error.status().message()),
                ::testing::Eq(NoSupportedCGroupMessage));
    GTEST_SKIP() << "Skipping test because the current machine does not support cgroup";
  }
  EXPECT_NE(monitor_or_error.value(), nullptr);
#else
  EXPECT_FALSE(monitor_or_error.ok());
#endif
}

TEST(CpuUtilizationMonitorFactoryTest, HostMonitorFunctional) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cpu_utilization");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  testing::NiceMock<Runtime::MockLoader> runtime;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor(), runtime);
  auto monitor_or_error = factory->createResourceMonitor(config, context);
  ASSERT_TRUE(monitor_or_error.ok());
  auto monitor = std::move(monitor_or_error.value());
  ASSERT_NE(monitor, nullptr);

  TestResourcePressureCallbacks callbacks;
  monitor->updateResourceUsage(callbacks);
  EXPECT_TRUE(callbacks.hasSuccess() || callbacks.hasError());
}

#if defined(__linux__)
TEST(CpuUtilizationMonitorFactoryTest, ContainerMonitorFunctional) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cpu_utilization");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  testing::NiceMock<Runtime::MockLoader> runtime;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor(), runtime);

  auto monitor_or_error = factory->createResourceMonitor(config, context);
  if (!monitor_or_error.ok()) {
    ASSERT_THAT(std::string(monitor_or_error.status().message()),
                ::testing::Eq(NoSupportedCGroupMessage));
    GTEST_SKIP() << "Skipping test because the current machine does not support cgroup";
  }
  auto monitor = std::move(monitor_or_error.value());
  ASSERT_NE(monitor, nullptr);

  TestResourcePressureCallbacks callbacks;
  monitor->updateResourceUsage(callbacks);
  EXPECT_TRUE(callbacks.hasSuccess() || callbacks.hasError());
}
#endif

TEST(CpuUtilizationMonitorFactoryTest, FactoryRegistered) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cpu_utilization");
  ASSERT_NE(factory, nullptr);

  EXPECT_EQ(factory->name(), "envoy.resource_monitors.cpu_utilization");
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
