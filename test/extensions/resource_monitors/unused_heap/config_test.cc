#include "envoy/config/resource_monitor/unused_heap/v2alpha/unused_heap.pb.h"
#include "envoy/config/resource_monitor/unused_heap/v2alpha/unused_heap.pb.validate.h"
#include "envoy/registry/registry.h"

#include "server/resource_monitor_config_impl.h"

#include "extensions/resource_monitors/unused_heap/config.h"

#include "test/mocks/event/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace UnusedHeapMonitor {
namespace {

TEST(UnusedHeapMonitorFactoryTest, CreateMonitor) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.unused_heap");
  EXPECT_NE(factory, nullptr);

  envoy::config::resource_monitor::unused_heap::v2alpha::UnusedHeapConfig config;
  config.set_max_unused_heap_size_bytes(std::numeric_limits<uint64_t>::max());
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
}

} // namespace
} // namespace UnusedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
