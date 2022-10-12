#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.h"
#include "envoy/extensions/resource_monitors/downstream_connections/v3/downstream_connections.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/resource_monitors/downstream_connections/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DownstreamConnections {
namespace {

TEST(ActiveDownstreamConnectionsMonitorFactoryTest, CreateMonitorInvalidConfig) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ProactiveResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.downstream_connections");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ("envoy.resource_monitors.downstream_connections", factory->name());

  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  config.set_max_active_downstream_connections(-1);
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_THROW_WITH_REGEX(factory->createProactiveResourceMonitor(config, context),
                          ProtoValidationException,
                          "Proto constraint validation failed "
                          "\\(DownstreamConnectionsConfigValidationError."
                          "MaxActiveDownstreamConnections: value must be greater than 0");
}

TEST(ActiveDownstreamConnectionsMonitorFactoryTest, CreateCustomMonitor) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ProactiveResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.downstream_connections");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ("envoy.resource_monitors.downstream_connections", factory->name());

  envoy::extensions::resource_monitors::downstream_connections::v3::DownstreamConnectionsConfig
      config;
  config.set_max_active_downstream_connections(1);
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = factory->createProactiveResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
}

TEST(ActiveDownstreamConnectionsMonitorFactoryTest, CreateDefaultMonitor) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ProactiveResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.downstream_connections");
  ASSERT_NE(factory, nullptr);

  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto config = factory->createEmptyConfigProto();

  EXPECT_THROW_WITH_REGEX(factory->createProactiveResourceMonitor(*config, context),
                          ProtoValidationException,
                          "Proto constraint validation failed "
                          "\\(DownstreamConnectionsConfigValidationError."
                          "MaxActiveDownstreamConnections: value must be greater than 0");
}

} // namespace
} // namespace DownstreamConnections
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
