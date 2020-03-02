#include "extensions/filters/listener/http_inspector/http_inspector.h"
#include "extensions/filters/listener/well_known_names.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {
namespace {

TEST(HttpInspectorConfigFactoryTest, TestCreateFactory) {
  Server::Configuration::NamedListenerFilterConfigFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedListenerFilterConfigFactory>::
          getFactory(ListenerFilters::ListenerFilterNames::get().HttpInspector);

  EXPECT_EQ(factory->name(), ListenerFilters::ListenerFilterNames::get().HttpInspector);

  const std::string yaml = R"EOF(
      {}
)EOF";

  ProtobufTypes::MessagePtr proto_config = factory->createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  Server::Configuration::MockListenerFactoryContext context;
  EXPECT_CALL(context, scope()).Times(1);
  Network::ListenerFilterFactoryCb cb =
      factory->createFilterFactoryFromProto(*proto_config, context);

  Network::MockListenerFilterManager manager;
  Network::ListenerFilterPtr added_filter;
  EXPECT_CALL(manager, addAcceptFilter_(_))
      .WillOnce(Invoke([&added_filter](Network::ListenerFilterPtr& filter) {
        added_filter = std::move(filter);
      }));
  cb(manager);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<HttpInspector::Filter*>(added_filter.get()), nullptr);
}

// Test that the deprecated extension name still functions.
TEST(HttpInspectorConfigFactoryTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.listener.http_inspector";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<
          Server::Configuration::NamedListenerFilterConfigFactory>::getFactory(deprecated_name));
}

} // namespace
} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
