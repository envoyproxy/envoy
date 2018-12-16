#include "envoy/config/filter/http/rate_limit/v2/rate_limit.pb.validate.h"

#include "common/config/filter_json.h"

#include "extensions/filters/common/ratelimit/ratelimit_registration.h"
#include "extensions/filters/http/ratelimit/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

// TODO(ramaraochavali): move to v2 config for all the tests.

TEST(RateLimitFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(RateLimitFilterConfig().createFilterFactoryFromProto(
                   envoy::config::filter::http::rate_limit::v2::RateLimit(), "stats", context),
               ProtoValidationException);
}

TEST(RateLimitFilterConfigTest, RateLimitFilterCorrectJson) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;

  // Return the same singleton manager as instance so that config can be found there.
  EXPECT_CALL(context, singletonManager()).WillOnce(ReturnRef(instance.singletonManager()));

  Filters::Common::RateLimit::ClientFactoryPtr client_factory =
      Filters::Common::RateLimit::rateLimitClientFactory(
          instance, instance.clusterManager().grpcAsyncClientManager(),
          envoy::config::bootstrap::v2::Bootstrap());

  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));

  RateLimitFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(RateLimitFilterConfigTest, RateLimitFilterCorrectProto) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config{};
  Envoy::Config::FilterJson::translateHttpRateLimitFilter(*json_config, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;

  // Return the same singleton manager as instance so that config can be found there.
  EXPECT_CALL(context, singletonManager()).WillOnce(ReturnRef(instance.singletonManager()));

  Filters::Common::RateLimit::ClientFactoryPtr client_factory =
      Filters::Common::RateLimit::rateLimitClientFactory(
          instance, instance.clusterManager().grpcAsyncClientManager(),
          envoy::config::bootstrap::v2::Bootstrap());

  EXPECT_CALL(context, clusterManager());
  EXPECT_CALL(context, runtime()).Times(1);
  EXPECT_CALL(context, scope()).Times(2);
  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));

  RateLimitFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(RateLimitFilterConfigTest, RateLimitFilterWithBootstrapOnlyConfig) {
  std::string yaml = R"EOF(
  domain: test
  timeout: 2s
  )EOF";

  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config{};
  MessageUtil::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;

  // Return the same singleton manager as instance so that config can be found there.
  EXPECT_CALL(context, singletonManager()).WillOnce(ReturnRef(instance.singletonManager()));

  envoy::config::bootstrap::v2::Bootstrap bootstrap_config;
  envoy::config::ratelimit::v2::RateLimitServiceConfig* ratelimit_config =
      bootstrap_config.mutable_rate_limit_service();
  envoy::api::v2::core::GrpcService* grpc_service = ratelimit_config->mutable_grpc_service();
  envoy::api::v2::core::GrpcService_EnvoyGrpc* envoy_grpc = grpc_service->mutable_envoy_grpc();
  envoy_grpc->set_cluster_name("ratelimit_cluster");

  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));

  Filters::Common::RateLimit::ClientFactoryPtr client_factory =
      Filters::Common::RateLimit::rateLimitClientFactory(
          instance, context.clusterManager().grpcAsyncClientManager(), bootstrap_config);

  RateLimitFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  // We do not expect client factory to be created - should use the one registered to singleton from
  // bootstrap.
  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .Times(0);
  cb(filter_callback);
}

TEST(RateLimitFilterConfigTest, RateLimitFilterWithServiceConfig) {
  std::string yaml = R"EOF(
  domain: test
  timeout: 2s
  rate_limit_service:
    grpc_service:
      envoy_grpc:
        cluster_name: ratelimit_cluster
  )EOF";

  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config{};
  MessageUtil::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;

  // Return the same singleton manager as instance so that config can be found there.
  EXPECT_CALL(context, singletonManager()).WillOnce(ReturnRef(instance.singletonManager()));

  Filters::Common::RateLimit::ClientFactoryPtr client_factory =
      Filters::Common::RateLimit::rateLimitClientFactory(
          instance, instance.clusterManager().grpcAsyncClientManager(),
          envoy::config::bootstrap::v2::Bootstrap());

  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));

  RateLimitFilterConfig factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(RateLimitFilterConfigTest, RateLimitFilterWithConflictingConfig) {
  std::string yaml = R"EOF(
  domain: test
  timeout: 2s
  rate_limit_service:
    grpc_service:
      envoy_grpc:
        cluster_name: ratelimit_cluster
  )EOF";

  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config{};
  MessageUtil::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;

  // Return the same singleton manager as instance so that config can be found there.
  EXPECT_CALL(context, singletonManager())
      .Times(1)
      .WillRepeatedly(ReturnRef(instance.singletonManager()));

  envoy::config::bootstrap::v2::Bootstrap bootstrap_config;
  envoy::config::ratelimit::v2::RateLimitServiceConfig* ratelimit_config =
      bootstrap_config.mutable_rate_limit_service();
  envoy::api::v2::core::GrpcService* grpc_service = ratelimit_config->mutable_grpc_service();
  envoy::api::v2::core::GrpcService_EnvoyGrpc* envoy_grpc = grpc_service->mutable_envoy_grpc();
  envoy_grpc->set_cluster_name("conflict_cluster");

  Filters::Common::RateLimit::ClientFactoryPtr client_factory =
      Filters::Common::RateLimit::rateLimitClientFactory(
          instance, instance.clusterManager().grpcAsyncClientManager(), bootstrap_config);

  RateLimitFilterConfig factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(proto_config, "stats", context),
                            EnvoyException,
                            "rate limit service config in filter does not match with bootstrap");
}

TEST(RateLimitFilterConfigTest, RateLimitFilterEmptyProto) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 1337
  }
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Server::MockInstance> instance;

  // Return the same singleton manager as instance so that config can be found there.
  EXPECT_CALL(context, singletonManager()).WillOnce(ReturnRef(instance.singletonManager()));

  Filters::Common::RateLimit::ClientFactoryPtr client_factory =
      Filters::Common::RateLimit::rateLimitClientFactory(
          instance, instance.clusterManager().grpcAsyncClientManager(),
          envoy::config::bootstrap::v2::Bootstrap());

  RateLimitFilterConfig factory;

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config =
      *dynamic_cast<envoy::config::filter::http::rate_limit::v2::RateLimit*>(
          factory.createEmptyConfigProto().get());
  Envoy::Config::FilterJson::translateHttpRateLimitFilter(*json_config, proto_config);

  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));

  Http::FilterFactoryCb cb = factory.createFilterFactory(*json_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(RateLimitFilterConfigTest, BadRateLimitFilterConfig) {
  std::string json_string = R"EOF(
  {
    "domain" : "test",
    "timeout_ms" : 0
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;

  RateLimitFilterConfig factory;
  EXPECT_THROW(factory.createFilterFactory(*json_config, "stats", context), Json::Exception);
}

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
