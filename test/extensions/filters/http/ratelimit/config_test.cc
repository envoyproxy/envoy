#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.validate.h"

#include "extensions/filters/http/ratelimit/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {
namespace {

TEST(RateLimitFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(RateLimitFilterConfig().createFilterFactoryFromProto(
                   envoy::extensions::filters::http::ratelimit::v3::RateLimit(), "stats", context),
               ProtoValidationException);
}

TEST(RateLimitFilterConfigTest, RatelimitCorrectProto) {
  const std::string yaml = R"EOF(
  domain: test
  timeout: 2s
  rate_limit_service:
    grpc_service:
      envoy_grpc:
        cluster_name: ratelimit_cluster
  )EOF";

  envoy::extensions::filters::http::ratelimit::v3::RateLimit proto_config{};
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));

  RateLimitFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(RateLimitFilterConfigTest, RateLimitFilterEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;

  RateLimitFilterConfig factory;

  envoy::extensions::filters::http::ratelimit::v3::RateLimit empty_proto_config =
      *dynamic_cast<envoy::extensions::filters::http::ratelimit::v3::RateLimit*>(
          factory.createEmptyConfigProto().get());

  EXPECT_THROW(factory.createFilterFactoryFromProto(empty_proto_config, "stats", context),
               EnvoyException);
}

TEST(RateLimitFilterConfigTest, BadRateLimitFilterConfig) {
  const std::string yaml = R"EOF(
  domain: foo
  route_key: my_route
  )EOF";

  envoy::extensions::filters::http::ratelimit::v3::RateLimit proto_config{};
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException,
                          "route_key: Cannot find field");
}

// Test that the deprecated extension name still functions.
TEST(RateLimitFilterConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.rate_limit";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace
} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
