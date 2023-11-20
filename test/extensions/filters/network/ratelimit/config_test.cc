#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.validate.h"

#include "source/extensions/filters/network/ratelimit/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

TEST(RateLimitFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  envoy::extensions::filters::network::ratelimit::v3::RateLimit rate_limit;
  rate_limit.mutable_rate_limit_service()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);
  EXPECT_THROW(RateLimitConfigFactory().createFilterFactoryFromProto(rate_limit, context),
               ProtoValidationException);
}

TEST(RateLimitFilterConfigTest, CorrectProto) {
  const std::string yaml = R"EOF(
  stat_prefix: my_stat_prefix
  domain: fake_domain
  descriptors:
    entries:
       key: my_key
       value: my_value
  timeout: 2s
  rate_limit_service:
    transport_api_version: V3
    grpc_service:
      envoy_grpc:
        cluster_name: ratelimit_cluster
  )EOF";

  envoy::extensions::filters::network::ratelimit::v3::RateLimit proto_config{};
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_CALL(context.server_factory_context_.cluster_manager_.async_client_manager_,
              getOrCreateRawAsyncClientWithHashKey(_, _, _))
      .WillOnce(Invoke([](const Grpc::GrpcServiceConfigWithHashKey&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
      }));

  RateLimitConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;

  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RateLimitFilterConfigTest, EmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;
  RateLimitConfigFactory factory;

  envoy::extensions::filters::network::ratelimit::v3::RateLimit empty_proto_config =
      *dynamic_cast<envoy::extensions::filters::network::ratelimit::v3::RateLimit*>(
          factory.createEmptyConfigProto().get());
  EXPECT_THROW(factory.createFilterFactoryFromProto(empty_proto_config, context), EnvoyException);
}

TEST(RateLimitFilterConfigTest, IncorrectProto) {
  std::string yaml_string = R"EOF(
stat_prefix: my_stat_prefix
domain: fake_domain
descriptors:
- entries:
  - key: my_key
    value: my_value
ip_allowlist: '12'
  )EOF";

  envoy::extensions::filters::network::ratelimit::v3::RateLimit proto_config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYaml(yaml_string, proto_config), EnvoyException,
                          "ip_allowlist");
}

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
