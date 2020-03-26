#include "extensions/filters/http/grpc_http1_bridge/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {
namespace {

TEST(GrpcHttp1BridgeFilterConfigTest, GrpcHttp1BridgeFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GrpcHttp1BridgeFilterConfig factory;
  envoy::extensions::filters::http::grpc_http1_bridge::v3::Config config;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

// Test that the deprecated extension name still functions.
TEST(GrpcHttp1BridgeFilterConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.grpc_http1_bridge";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace
} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
