#include "source/extensions/filters/listener/connect_handler/connect_handler.h"

#include "test/mocks/server/listener_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ConnectHandler {
namespace {

TEST(ConnectHandlerConfigFactoryTest, TestCreateFactory) {
  const std::string ConnectHandler = "envoy.filters.listener.connect_handler";
  Server::Configuration::NamedListenerFilterConfigFactory* factory = Registry::FactoryRegistry<
      Server::Configuration::NamedListenerFilterConfigFactory>::getFactory(ConnectHandler);

  EXPECT_EQ(factory->name(), ConnectHandler);

  const std::string yaml = R"EOF(
      {}
)EOF";

  ProtobufTypes::MessagePtr proto_config = factory->createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  Server::Configuration::MockListenerFactoryContext context;
  EXPECT_CALL(context, scope());
  Network::ListenerFilterFactoryCb cb =
      factory->createListenerFilterFactoryFromProto(*proto_config, nullptr, context);

  Network::MockListenerFilterManager manager;
  Network::ListenerFilterPtr added_filter;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&added_filter](const Network::ListenerFilterMatcherSharedPtr&,
                                       Network::ListenerFilterPtr& filter) {
        added_filter = std::move(filter);
      }));
  cb(manager);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<ConnectHandler::Filter*>(added_filter.get()), nullptr);
}

// Test that the deprecated extension name is disabled by default.
// TODO(zuercher): remove when envoy.deprecated_features.allow_deprecated_extension_names is removed
TEST(ConnectHandlerConfigFactoryTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.listener.connect_handler";

  ASSERT_EQ(
      nullptr,
      Registry::FactoryRegistry<
          Server::Configuration::NamedListenerFilterConfigFactory>::getFactory(deprecated_name));
}

} // namespace
} // namespace ConnectHandler
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
