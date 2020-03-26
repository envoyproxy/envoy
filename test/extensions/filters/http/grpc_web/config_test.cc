#include "extensions/filters/http/grpc_web/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {
namespace {

TEST(GrpcWebFilterConfigTest, GrpcWebFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GrpcWebFilterConfig factory;
  envoy::extensions::filters::http::grpc_web::v3::GrpcWeb config;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

// Test that the deprecated extension name still functions.
TEST(GrpcWebFilterConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.grpc_web";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace
} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
