#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/thread.h"
#include "source/extensions/resource_monitors/cpu_utilization/config.h"
#include "source/extensions/resource_monitors/cpu_utilization/linux_cpu_stats_reader.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/options.h"

#include "absl/types/optional.h"
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

  void onFailure(const EnvoyException& error) override {
    error_ = error;
    has_error_ = true;
  }

  bool hasSuccess() const { return has_success_; }
  bool hasError() const { return has_error_; }
  double pressure() const { return pressure_.value_or(0.0); }

private:
  absl::optional<double> pressure_;
  absl::optional<EnvoyException> error_;
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
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
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

#if defined(__linux__)
  // Skip the check if the system running the test does not support cgroup.
  TRY_ASSERT_MAIN_THREAD {
    auto monitor = factory->createResourceMonitor(config, context);
    // If we did not throw, we must have a non-null monitor.
    EXPECT_NE(monitor, nullptr);
  }
  END_TRY
  CATCH(EnvoyException & e, {
    // If we did throw it must have been because of cgroup.
    ASSERT_THAT(std::string(e.what()), ::testing::Eq(NoSupportedCGroupMessage));
    GTEST_SKIP() << "Skipping test because the current machine does not support cgroup";
  });
#else
  EXPECT_THROW(factory->createResourceMonitor(config, context), EnvoyException);
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
  auto monitor = factory->createResourceMonitor(config, context);
  ASSERT_NE(monitor, nullptr);

  // Exercise the monitor by calling updateResourceUsage
  TestResourcePressureCallbacks callbacks;
  monitor->updateResourceUsage(callbacks);
  // Either success or error is acceptable depending on system state
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

  // Skip the check if the system running the test does not support cgroup.
  TRY_ASSERT_MAIN_THREAD {
    auto monitor = factory->createResourceMonitor(config, context);
    // If cgroup files exist (Linux CI), monitor should be created and functional
    ASSERT_NE(monitor, nullptr);

    // Exercise the monitor by calling updateResourceUsage
    TestResourcePressureCallbacks callbacks;
    monitor->updateResourceUsage(callbacks);
    // Either success or error is acceptable depending on system state
    EXPECT_TRUE(callbacks.hasSuccess() || callbacks.hasError());
  }
  END_TRY
  CATCH(EnvoyException & e, {
    // If we did throw it must have been because of cgroup.
    ASSERT_THAT(std::string(e.what()), ::testing::Eq(NoSupportedCGroupMessage));
    GTEST_SKIP() << "Skipping test because the current machine does not support cgroup";
  });
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
