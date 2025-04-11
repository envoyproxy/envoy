#include "envoy/registry/registry.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/cgroup_memory/config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

TEST(CgroupMemoryMonitorFactoryTest, BasicTest) {
  std::cout << "Running CgroupMemoryMonitorFactory basic test" << std::endl;
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
