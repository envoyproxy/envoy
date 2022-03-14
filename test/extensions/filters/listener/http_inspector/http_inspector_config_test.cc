#include "source/extensions/filters/listener/http_inspector/http_inspector.h"

#include "test/mocks/server/listener_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {
namespace {

TEST(HttpInspectorConfigFactoryTest, TestCreateFactory) {
  const std::string HttpInspector = "envoy.filters.listener.http_inspector";
  Server::Configuration::NamedListenerFilterConfigFactory* factory = Registry::FactoryRegistry<
      Server::Configuration::NamedListenerFilterConfigFactory>::getFactory(HttpInspector);

  EXPECT_EQ(factory->name(), HttpInspector);

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
  EXPECT_NE(dynamic_cast<HttpInspector::Filter*>(added_filter.get()), nullptr);
}

} // namespace
} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
