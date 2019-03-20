#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.validate.h"

#include "common/config/filter_json.h"

#include "extensions/filters/network/ratelimit/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

TEST(RateLimitFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(RateLimitConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::network::rate_limit::v2::RateLimit(), context),
               ProtoValidationException);
}

TEST(RateLimitFilterConfigTest, RatelimitCorrectProto) {
  const std::string yaml = R"EOF(
  stat_prefix: my_stat_prefix
  domain: fake_domain
  descriptors:
    entries:
       key: my_key
       value: my_value
  timeout: 2s
  rate_limit_service:
    grpc_service:
      envoy_grpc:
        cluster_name: ratelimit_cluster
  )EOF";

  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config{};
  MessageUtil::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));

  RateLimitConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RateLimitFilterConfigTest, RateLimitFilterEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;
  RateLimitConfigFactory factory;

  envoy::config::filter::network::rate_limit::v2::RateLimit empty_proto_config =
      *dynamic_cast<envoy::config::filter::network::rate_limit::v2::RateLimit*>(
          factory.createEmptyConfigProto().get());
  EXPECT_THROW(factory.createFilterFactoryFromProto(empty_proto_config, context), EnvoyException);
}

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
