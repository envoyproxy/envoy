#include "envoy/extensions/resource_monitors/injected_resource/v3/injected_resource.pb.h"
#include "envoy/extensions/resource_monitors/injected_resource/v3/injected_resource.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/extensions/resource_monitors/injected_resource/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/server/options.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace InjectedResourceMonitor {
namespace {

TEST(InjectedResourceMonitorFactoryTest, CreateMonitor) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.injected_resource");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::injected_resource::v3::InjectedResourceConfig config;
  config.set_filename(TestEnvironment::temporaryPath("injected_resource"));
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      *dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  Server::ResourceMonitorPtr monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
}

} // namespace
} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
