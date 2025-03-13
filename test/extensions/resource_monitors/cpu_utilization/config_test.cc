#include "envoy/extensions/resource_monitors/cpu_utilization/v3/cpu_utilization.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/resource_monitors/cpu_utilization/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/options.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {
namespace {

TEST(CpuUtilizationMonitorFactoryTest, CreateMonitorDefault) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cpu_utilization");
  EXPECT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  EXPECT_EQ(config.mode(),
            envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::HOST);
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
}

TEST(CpuUtilizationMonitorFactoryTest, CreateContainerCPUMonitor) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cpu_utilization");
  EXPECT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig config;
  config.set_mode(
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  EXPECT_EQ(
      config.mode(),
      envoy::extensions::resource_monitors::cpu_utilization::v3::CpuUtilizationConfig::CONTAINER);
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
