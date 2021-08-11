#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"
#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/resource_monitors/fixed_heap/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/options.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {
namespace {

TEST(FixedHeapMonitorFactoryTest, CreateMonitor) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  EXPECT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.set_max_heap_size_bytes(std::numeric_limits<uint64_t>::max());
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
}

} // namespace
} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
