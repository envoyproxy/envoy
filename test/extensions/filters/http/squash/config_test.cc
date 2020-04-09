#include "envoy/extensions/filters/http/squash/v3/squash.pb.h"
#include "envoy/extensions/filters/http/squash/v3/squash.pb.validate.h"

#include "extensions/filters/http/squash/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Squash {
namespace {

TEST(SquashFilterConfigFactoryTest, SquashFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
  cluster: fake_cluster
  attachment_template:
    a: b
  request_timeout: 1.001s
  attachment_poll_period: 2.002s
  attachment_timeout: 3.003s
  )EOF";

  envoy::extensions::filters::http::squash::v3::Squash proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  SquashFilterConfigFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

// Test that the deprecated extension name still functions.
TEST(SquashFilterConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.squash";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace
} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
