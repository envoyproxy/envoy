#include "envoy/extensions/resource_monitors/envoy_container_cpu_utilization/v3/envoy_container_cpu_utilization.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/resource_monitors/envoy_container_cpu_utilization/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/options.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace EnvoyContainerCpuUtilizationMonitor {
namespace {

TEST(EnvoyContainerCpuUtilizationMonitorFactoryTest, CreateMonitor) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.envoy_container_cpu_utilization");
  EXPECT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::envoy_container_cpu_utilization::v3::EnvoyContainerCpuUtilizationConfig config;
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
}

} // namespace
} // namespace EnvoyContainerCpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
