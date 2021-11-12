#include "source/extensions/filters/listener/original_src/config.h"
#include "source/extensions/filters/listener/original_src/original_src.h"
#include "source/extensions/filters/listener/original_src/original_src_config_factory.h"

#include "test/mocks/server/listener_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {
namespace {

TEST(OriginalSrcConfigFactoryTest, TestCreateFactory) {
  std::string yaml = R"EOF(
    mark: 5
    bind_port: true
)EOF";

  OriginalSrcConfigFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockListenerFactoryContext> context;

  Network::ListenerFilterFactoryCb cb =
      factory.createListenerFilterFactoryFromProto(*proto_config, nullptr, context);
  Network::MockListenerFilterManager manager;
  Network::ListenerFilterPtr added_filter;
  EXPECT_CALL(manager, addAcceptFilter_(_, _))
      .WillOnce(Invoke([&added_filter](const Network::ListenerFilterMatcherSharedPtr&,
                                       Network::ListenerFilterPtr& filter) {
        added_filter = std::move(filter);
      }));
  cb(manager);

  // Make sure we actually create the correct type!
  EXPECT_NE(dynamic_cast<OriginalSrcFilter*>(added_filter.get()), nullptr);
}

// Test that the deprecated extension name is disabled by default.
// TODO(zuercher): remove when envoy.deprecated_features.allow_deprecated_extension_names is removed
TEST(OriginalSrcConfigFactoryTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.listener.original_src";

  ASSERT_EQ(
      nullptr,
      Registry::FactoryRegistry<
          Server::Configuration::NamedListenerFilterConfigFactory>::getFactory(deprecated_name));
}

} // namespace
} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
