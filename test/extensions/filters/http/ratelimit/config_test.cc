#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.validate.h"

#include "source/extensions/filters/http/ratelimit/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"

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
  envoy::extensions::filters::http::ratelimit::v3::RateLimit config;
  config.mutable_rate_limit_service()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);
  EXPECT_THROW(
      RateLimitFilterConfig().createFilterFactoryFromProto(config, "stats", context).value(),
      ProtoValidationException);
}

TEST(RateLimitFilterConfigTest, RatelimitCorrectProto) {
  const std::string yaml = R"EOF(
  domain: test
  timeout: 2s
  rate_limit_service:
    transport_api_version: V3
    grpc_service:
      envoy_grpc:
        cluster_name: ratelimit_cluster
  )EOF";

  envoy::extensions::filters::http::ratelimit::v3::RateLimit proto_config{};
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_CALL(context.server_factory_context_.cluster_manager_.async_client_manager_,
              getOrCreateRawAsyncClientWithHashKey(_, _, _))
      .WillOnce(Invoke([](const Grpc::GrpcServiceConfigWithHashKey&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
      }));

  RateLimitFilterConfig factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
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

  EXPECT_THROW(factory.createFilterFactoryFromProto(empty_proto_config, "stats", context).value(),
               EnvoyException);
}

TEST(RateLimitFilterConfigTest, BadRateLimitFilterConfig) {
  const std::string yaml = R"EOF(
  domain: foo
  route_key: my_route
  )EOF";

  envoy::extensions::filters::http::ratelimit::v3::RateLimit proto_config{};
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

} // namespace
} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
