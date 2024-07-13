#include "envoy/api/v2/cluster.pb.h"
#include "envoy/common/exception.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/cors/v3/cors.pb.h"

#include "source/common/common/fmt.h"
#include "source/common/config/api_version.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/protobuf/protobuf.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "udpa/type/v1/typed_struct.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

using testing::ContainsRegex;
using testing::Eq;
using testing::HasSubstr;
using testing::Optional;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

TEST(UtilityTest, ComputeHashedVersion) {
  EXPECT_EQ("hash_2e1472b57af294d1", Utility::computeHashedVersion("{}").first);
  EXPECT_EQ("hash_33bf00a859c4ba3f", Utility::computeHashedVersion("foo").first);
}

TEST(UtilityTest, ConfigSourceDefaultInitFetchTimeout) {
  envoy::config::core::v3::ConfigSource config_source;
  EXPECT_EQ(15000, Utility::configSourceInitialFetchTimeout(config_source).count());
}

TEST(UtilityTest, ConfigSourceInitFetchTimeout) {
  envoy::config::core::v3::ConfigSource config_source;
  config_source.mutable_initial_fetch_timeout()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(654));
  EXPECT_EQ(654, Utility::configSourceInitialFetchTimeout(config_source).count());
}

TEST(UtilityTest, CheckFilesystemSubscriptionBackingPath) {
  Api::ApiPtr api = Api::createApiForTest();

  EXPECT_EQ(Utility::checkFilesystemSubscriptionBackingPath("foo", *api).message(),
            "paths must refer to an existing path in the system: 'foo' does not exist");
  std::string test_path = TestEnvironment::temporaryDirectory();
  EXPECT_TRUE(Utility::checkFilesystemSubscriptionBackingPath(test_path, *api).ok());
}

TEST(UtilityTest, ParseDefaultRateLimitSettings) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  const absl::StatusOr<RateLimitSettings> rate_limit_settings =
      Utility::parseRateLimitSettings(api_config_source);
  EXPECT_TRUE(rate_limit_settings.ok());
  EXPECT_EQ(false, rate_limit_settings->enabled_);
  EXPECT_EQ(100, rate_limit_settings->max_tokens_);
  EXPECT_EQ(10, rate_limit_settings->fill_rate_);
}

TEST(UtilityTest, ParseEmptyRateLimitSettings) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  api_config_source.mutable_rate_limit_settings();
  const absl::StatusOr<RateLimitSettings> rate_limit_settings =
      Utility::parseRateLimitSettings(api_config_source);
  EXPECT_TRUE(rate_limit_settings.ok());
  EXPECT_EQ(true, rate_limit_settings->enabled_);
  EXPECT_EQ(100, rate_limit_settings->max_tokens_);
  EXPECT_EQ(10, rate_limit_settings->fill_rate_);
}

TEST(UtilityTest, ParseRateLimitSettings) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  envoy::config::core::v3::RateLimitSettings* rate_limits =
      api_config_source.mutable_rate_limit_settings();
  rate_limits->mutable_max_tokens()->set_value(500);
  rate_limits->mutable_fill_rate()->set_value(4);
  const absl::StatusOr<RateLimitSettings> rate_limit_settings =
      Utility::parseRateLimitSettings(api_config_source);
  EXPECT_TRUE(rate_limit_settings.ok());
  EXPECT_EQ(true, rate_limit_settings->enabled_);
  EXPECT_EQ(500, rate_limit_settings->max_tokens_);
  EXPECT_EQ(4, rate_limit_settings->fill_rate_);
}

TEST(UtilityTest, ParseNanFillRateLimitSettings) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  envoy::config::core::v3::RateLimitSettings* rate_limits =
      api_config_source.mutable_rate_limit_settings();
  rate_limits->mutable_max_tokens()->set_value(500);
  rate_limits->mutable_fill_rate()->set_value(std::numeric_limits<double>::quiet_NaN());
  const absl::StatusOr<RateLimitSettings> rate_limit_settings =
      Utility::parseRateLimitSettings(api_config_source);
  EXPECT_FALSE(rate_limit_settings.ok());
  EXPECT_EQ(rate_limit_settings.status().message(),
            "The value of fill_rate in RateLimitSettings (nan) must not be NaN nor Inf");
}

TEST(UtilityTest, ParseInfiniteFillRateLimitSettings) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  envoy::config::core::v3::RateLimitSettings* rate_limits =
      api_config_source.mutable_rate_limit_settings();
  rate_limits->mutable_max_tokens()->set_value(500);
  rate_limits->mutable_fill_rate()->set_value(std::numeric_limits<double>::infinity());
  const absl::StatusOr<RateLimitSettings> rate_limit_settings =
      Utility::parseRateLimitSettings(api_config_source);
  EXPECT_FALSE(rate_limit_settings.ok());
  EXPECT_EQ(rate_limit_settings.status().message(),
            "The value of fill_rate in RateLimitSettings (inf) must not be NaN nor Inf");
}

// TEST(UtilityTest, FactoryForGrpcApiConfigSource) should catch misconfigured
// API configs along the dimension of ApiConfigSource type.
TEST(UtilityTest, FactoryForGrpcApiConfigSource) {
  NiceMock<Grpc::MockAsyncClientManager> async_client_manager;
  Stats::MockStore store;
  Stats::Scope& scope = *store.rootScope();

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    EXPECT_THAT(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 0)
                    .status()
                    .message(),
                HasSubstr("API configs must have either a gRPC service or a cluster name defined"));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services();
    api_config_source.add_grpc_services();
    EXPECT_THAT(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 0)
                    .status()
                    .message(),
                ContainsRegex(fmt::format(
                    "{}::.DELTA_.GRPC must have no more than 1 gRPC services specified:",
                    api_config_source.GetTypeName())));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_cluster_names();
    // this also logs a warning for setting REST cluster names for a gRPC API config.
    EXPECT_THAT(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 0)
                    .status()
                    .message(),
                ContainsRegex(fmt::format("{}::.DELTA_.GRPC must not have a cluster name "
                                          "specified:",
                                          api_config_source.GetTypeName())));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_cluster_names();
    api_config_source.add_cluster_names();
    EXPECT_THAT(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 0)
                    .status()
                    .message(),
                ContainsRegex(fmt::format("{}::.DELTA_.GRPC must not have a cluster name "
                                          "specified:",
                                          api_config_source.GetTypeName())));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    // this also logs a warning for configuring gRPC clusters for a REST API config.
    EXPECT_THAT(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 0)
                    .status()
                    .message(),
                ContainsRegex(fmt::format("{}, if not a gRPC type, must not have a gRPC service "
                                          "specified:",
                                          api_config_source.GetTypeName())));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
    api_config_source.add_cluster_names("foo");
    EXPECT_THAT(
        Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope,
                                               false, 0)
            .status()
            .message(),
        ContainsRegex(fmt::format("{} type must be gRPC:", api_config_source.GetTypeName())));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    envoy::config::core::v3::GrpcService expected_grpc_service;
    expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(async_client_manager,
                factoryForGrpcService(ProtoEq(expected_grpc_service), Ref(scope), false));
    EXPECT_TRUE(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 0)
                    .ok());
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(
        async_client_manager,
        factoryForGrpcService(ProtoEq(api_config_source.grpc_services(0)), Ref(scope), true));
    EXPECT_TRUE(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, true, 0)
                    .ok());
  }
}

// Validates that when failover is supported, the validation works as expected.
TEST(UtilityTest, FactoryForGrpcApiConfigSourceWithFailover) {
  // When envoy.restart_features.xds_failover_support is deprecated, the
  // following tests will need to be merged with the tests in
  // FactoryForGrpcApiConfigSource.
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.xds_failover_support", "true"}});

  NiceMock<Grpc::MockAsyncClientManager> async_client_manager;
  Stats::MockStore store;
  Stats::Scope& scope = *store.rootScope();

  // No more than 2 config sources.
  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services();
    api_config_source.add_grpc_services();
    api_config_source.add_grpc_services();
    EXPECT_THAT(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 0)
                    .status()
                    .message(),
                ContainsRegex(fmt::format(
                    "{}::.DELTA_.GRPC must have no more than 2 gRPC services specified:",
                    api_config_source.GetTypeName())));
  }

  // A single gRPC service is valid.
  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    envoy::config::core::v3::GrpcService expected_grpc_service;
    expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(async_client_manager,
                factoryForGrpcService(ProtoEq(expected_grpc_service), Ref(scope), false));
    EXPECT_TRUE(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 0)
                    .ok());
  }

  // 2 gRPC services is valid.
  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("bar");

    envoy::config::core::v3::GrpcService expected_grpc_service_foo;
    expected_grpc_service_foo.mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(async_client_manager,
                factoryForGrpcService(ProtoEq(expected_grpc_service_foo), Ref(scope), false));
    EXPECT_TRUE(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 0)
                    .ok());

    envoy::config::core::v3::GrpcService expected_grpc_service_bar;
    expected_grpc_service_bar.mutable_envoy_grpc()->set_cluster_name("bar");
    EXPECT_CALL(async_client_manager,
                factoryForGrpcService(ProtoEq(expected_grpc_service_bar), Ref(scope), false));
    EXPECT_TRUE(Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source,
                                                       scope, false, 1)
                    .ok());
  }
}

TEST(UtilityTest, PrepareDnsRefreshStrategy) {
  NiceMock<Random::MockRandomGenerator> random;

  {
    // dns_failure_refresh_rate not set.
    envoy::config::cluster::v3::Cluster cluster;
    BackOffStrategyPtr strategy =
        Utility::prepareDnsRefreshStrategy<envoy::config::cluster::v3::Cluster>(cluster, 5000,
                                                                                random);
    EXPECT_NE(nullptr, dynamic_cast<FixedBackOffStrategy*>(strategy.get()));
  }

  {
    // dns_failure_refresh_rate set.
    envoy::config::cluster::v3::Cluster cluster;
    cluster.mutable_dns_failure_refresh_rate()->mutable_base_interval()->set_seconds(7);
    cluster.mutable_dns_failure_refresh_rate()->mutable_max_interval()->set_seconds(10);
    BackOffStrategyPtr strategy =
        Utility::prepareDnsRefreshStrategy<envoy::config::cluster::v3::Cluster>(cluster, 5000,
                                                                                random);
    EXPECT_NE(nullptr, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get()));
  }

  {
    // dns_failure_refresh_rate set with invalid max_interval.
    envoy::config::cluster::v3::Cluster cluster;
    cluster.mutable_dns_failure_refresh_rate()->mutable_base_interval()->set_seconds(7);
    cluster.mutable_dns_failure_refresh_rate()->mutable_max_interval()->set_seconds(2);
    EXPECT_THROW_WITH_REGEX(Utility::prepareDnsRefreshStrategy<envoy::config::cluster::v3::Cluster>(
                                cluster, 5000, random),
                            EnvoyException,
                            "dns_failure_refresh_rate must have max_interval greater than "
                            "or equal to the base_interval");
  }
}

// test that default values are used correctly when no retry configuration is provided
TEST(UtilityTest, PrepareJitteredExponentialBackOffStrategyNoConfig) {
  NiceMock<Random::MockRandomGenerator> random;
  // test prepareJitteredExponentialBackOffStrategy method with only default values
  {
    envoy::config::core::v3::GrpcService::EnvoyGrpc config;

    // valid default base and max interval values
    JitteredExponentialBackOffStrategyPtr strategy;
    strategy =
        Utility::prepareJitteredExponentialBackOffStrategy(config, random, 500, 1000).value();

    EXPECT_NE(nullptr, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get()));
    EXPECT_EQ(true, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())
                        ->isOverTimeLimit(1000 + 1));

    // only valid base interval value
    strategy =
        Utility::prepareJitteredExponentialBackOffStrategy(config, random, 500, absl::nullopt)
            .value();

    EXPECT_NE(nullptr, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get()));
    // time limit will be 10 * provided default base interval
    EXPECT_EQ(true, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())
                        ->isOverTimeLimit(500 * 10 + 1));

    // invalid base interval value
    EXPECT_EQ(Utility::prepareJitteredExponentialBackOffStrategy(config, random, 0, absl::nullopt)
                  .status()
                  .message(),
              "default_base_interval_ms must be greater than zero");

    // invalid max interval value < base interval value
    EXPECT_EQ(
        Utility::prepareJitteredExponentialBackOffStrategy(config, random, 1000, 500)
            .status()
            .message(),
        "default_max_interval_ms must be greater than or equal to the default_base_interval_ms");
  }

  // provide Envoy Grpc Config without any configured retry values
  {
    envoy::config::core::v3::GrpcService::EnvoyGrpc config;
    const std::string config_yaml = R"EOF(
        cluster_name: some_xds_cluster
    )EOF";

    TestUtility::loadFromYaml(config_yaml, config);
    EXPECT_FALSE(config.has_retry_policy());

    JitteredExponentialBackOffStrategyPtr strategy =
        Utility::prepareJitteredExponentialBackOffStrategy(config, random, 500, absl::nullopt)
            .value();

    EXPECT_NE(nullptr, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get()));
    // time limit will be 10 * provided default base interval
    EXPECT_EQ(true, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())
                        ->isOverTimeLimit(500 * 10 + 1));

    // test an invalid default base interval
    EXPECT_EQ(Utility::prepareJitteredExponentialBackOffStrategy(config, random, 0, absl::nullopt)
                  .status()
                  .message(),
              "default_base_interval_ms must be greater than zero");
  }

  // provide ApiConfigSource config without any configured retry values
  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    const std::string config_yaml = R"EOF(
      api_type: GRPC
    )EOF";

    TestUtility::loadFromYaml(config_yaml, api_config_source);

    JitteredExponentialBackOffStrategyPtr strategy =
        Utility::prepareJitteredExponentialBackOffStrategy(api_config_source, random, 500,
                                                           absl::nullopt)
            .value();

    EXPECT_NE(nullptr, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get()));
    // time limit will be 10 * provided default base interval
    EXPECT_EQ(true, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())
                        ->isOverTimeLimit(500 * 10 + 1));

    // test an invalid default base interval
    EXPECT_EQ(Utility::prepareJitteredExponentialBackOffStrategy(api_config_source, random, 0,
                                                                 absl::nullopt)
                  .status()
                  .message(),
              "default_base_interval_ms must be greater than zero");
  }
}

// confirm that user provided values in the retry configuration are correctly used to prepare the
// backoff strategy
TEST(UtilityTest, PrepareJitteredExponentialBackOffStrategyConfigFileValues) {
  NiceMock<Random::MockRandomGenerator> random;
  // Provide config values for retry
  {
    envoy::config::core::v3::GrpcService::EnvoyGrpc config;
    const std::string config_yaml = R"EOF(
        cluster_name: some_xds_cluster
        retry_policy:
          retry_back_off:
            base_interval: 0.01s
            max_interval: 10s
    )EOF";
    TestUtility::loadFromYaml(config_yaml, config);
    EXPECT_TRUE(config.has_retry_policy());
    JitteredExponentialBackOffStrategyPtr strategy =
        Utility::prepareJitteredExponentialBackOffStrategy(config, random, 500, absl::nullopt)
            .value();
    EXPECT_NE(nullptr, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get()));
    EXPECT_EQ(
        false,
        dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())->isOverTimeLimit(10000));
    EXPECT_EQ(
        true,
        dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())->isOverTimeLimit(10001));
  }

  // Provide ApiConfigSource config values for retry
  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    const std::string config_yaml = R"EOF(
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: some_xds_cluster
          retry_policy:
            retry_back_off:
              base_interval: 0.01s
              max_interval: 10s
    )EOF";

    TestUtility::loadFromYaml(config_yaml, api_config_source);

    JitteredExponentialBackOffStrategyPtr strategy =
        Utility::prepareJitteredExponentialBackOffStrategy(api_config_source, random, 500,
                                                           absl::nullopt)
            .value();

    EXPECT_NE(nullptr, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get()));

    EXPECT_EQ(
        false,
        dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())->isOverTimeLimit(10000));

    EXPECT_EQ(
        true,
        dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())->isOverTimeLimit(10001));
  }
}

// test out various retry backoff timer value scenarios (1. valid base and max intervals, 2. only
// base interval, 3. max interval < base interval)
TEST(UtilityTest, PrepareJitteredExponentialBackOffStrategyCustomValues) {
  NiceMock<Random::MockRandomGenerator> random;
  {
    // set custom values for both base and max interval
    {
      uint64_t test_base_interval_ms = 5000;
      uint64_t test_max_interval_ms = 20000;

      // Provide config values for retry
      envoy::config::core::v3::GrpcService::EnvoyGrpc config;
      const std::string config_yaml = R"EOF(
        cluster_name: some_xds_cluster
    )EOF";
      TestUtility::loadFromYaml(config_yaml, config);

      config.mutable_retry_policy()->mutable_retry_back_off()->mutable_base_interval()->set_seconds(
          test_base_interval_ms / 1000);
      config.mutable_retry_policy()->mutable_retry_back_off()->mutable_max_interval()->set_seconds(
          test_max_interval_ms / 1000);

      JitteredExponentialBackOffStrategyPtr strategy =
          Utility::prepareJitteredExponentialBackOffStrategy(config, random, 500, absl::nullopt)
              .value();

      // provided time limit is equal to max time limit
      EXPECT_EQ(false, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())
                           ->isOverTimeLimit(test_max_interval_ms));

      // provided time limit is over max time limit
      EXPECT_EQ(true, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())
                          ->isOverTimeLimit(test_max_interval_ms + 1));
    }

    // only set base_interval
    {
      // Provide config values for retry
      envoy::config::core::v3::GrpcService::EnvoyGrpc config;
      const std::string config_yaml = R"EOF(
        cluster_name: some_xds_cluster
    )EOF";
      TestUtility::loadFromYaml(config_yaml, config);

      uint64_t test_base_interval_ms = 5000;

      config.mutable_retry_policy()->mutable_retry_back_off()->mutable_base_interval()->set_seconds(
          test_base_interval_ms / 1000);

      JitteredExponentialBackOffStrategyPtr strategy =
          Utility::prepareJitteredExponentialBackOffStrategy(config, random, 500, absl::nullopt)
              .value();

      // max_interval should be less than or equal test_base_interval * 10
      EXPECT_EQ(false, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())
                           ->isOverTimeLimit(test_base_interval_ms * 10));
      EXPECT_EQ(true, dynamic_cast<JitteredExponentialBackOffStrategy*>(strategy.get())
                          ->isOverTimeLimit(test_base_interval_ms * 10 + 1));
    }

    // set max_interval < base_interval
    {
      uint64_t test_base_interval_ms = 10000;
      uint64_t test_max_interval_ms = 5000;

      // Provide config values for retry
      envoy::config::core::v3::GrpcService::EnvoyGrpc config;
      const std::string config_yaml = R"EOF(
        cluster_name: some_xds_cluster
    )EOF";
      TestUtility::loadFromYaml(config_yaml, config);

      config.mutable_retry_policy()->mutable_retry_back_off()->mutable_base_interval()->set_seconds(
          test_base_interval_ms);
      config.mutable_retry_policy()->mutable_retry_back_off()->mutable_max_interval()->set_seconds(
          test_max_interval_ms);

      EXPECT_FALSE(
          Utility::prepareJitteredExponentialBackOffStrategy(config, random, 500, absl::nullopt)
              .status()
              .ok());
    }
  }
}

// Validate that an opaque config of the wrong type throws during conversion.
TEST(UtilityTest, AnyWrongType) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  ProtobufWkt::Any typed_config;
  typed_config.PackFrom(source_duration);
  ProtobufWkt::Timestamp out;
  EXPECT_THROW_WITH_REGEX(
      Utility::translateOpaqueConfig(typed_config, ProtobufMessage::getStrictValidationVisitor(),
                                     out),
      EnvoyException,
      R"(Unable to unpack as google.protobuf.Timestamp:.*[\n]*\[type.googleapis.com/google.protobuf.Duration\] .*)");
}

TEST(UtilityTest, TranslateAnyWrongToFactoryConfig) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  ProtobufWkt::Any typed_config;
  typed_config.PackFrom(source_duration);

  MockTypedFactory factory;
  EXPECT_CALL(factory, createEmptyConfigProto()).WillOnce(Invoke([]() -> ProtobufTypes::MessagePtr {
    return ProtobufTypes::MessagePtr{new ProtobufWkt::Timestamp()};
  }));

  EXPECT_THROW_WITH_REGEX(
      Utility::translateAnyToFactoryConfig(typed_config,
                                           ProtobufMessage::getStrictValidationVisitor(), factory),
      EnvoyException,
      R"(Unable to unpack as google.protobuf.Timestamp:.*[\n]*\[type.googleapis.com/google.protobuf.Duration\] .*)");
}

TEST(UtilityTest, TranslateAnyToFactoryConfig) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  ProtobufWkt::Any typed_config;
  typed_config.PackFrom(source_duration);

  MockTypedFactory factory;
  EXPECT_CALL(factory, createEmptyConfigProto()).WillOnce(Invoke([]() -> ProtobufTypes::MessagePtr {
    return ProtobufTypes::MessagePtr{new ProtobufWkt::Duration()};
  }));

  auto config = Utility::translateAnyToFactoryConfig(
      typed_config, ProtobufMessage::getStrictValidationVisitor(), factory);

  EXPECT_THAT(*config, ProtoEq(source_duration));
}

template <typename T> class UtilityTypedStructTest : public ::testing::Test {
public:
  static void packTypedStructIntoAny(ProtobufWkt::Any& typed_config,
                                     const Protobuf::Message& inner) {
    T typed_struct;
    (*typed_struct.mutable_type_url()) =
        absl::StrCat("type.googleapis.com/", inner.GetDescriptor()->full_name());
    MessageUtil::jsonConvert(inner, *typed_struct.mutable_value());
    typed_config.PackFrom(typed_struct);
  }
};

using TypedStructTypes = ::testing::Types<xds::type::v3::TypedStruct, udpa::type::v1::TypedStruct>;
TYPED_TEST_SUITE(UtilityTypedStructTest, TypedStructTypes);

// Verify that TypedStruct can be translated into google.protobuf.Struct
TYPED_TEST(UtilityTypedStructTest, TypedStructToStruct) {
  ProtobufWkt::Any typed_config;
  ProtobufWkt::Struct untyped_struct;
  (*untyped_struct.mutable_fields())["foo"].set_string_value("bar");
  this->packTypedStructIntoAny(typed_config, untyped_struct);

  ProtobufWkt::Struct out;
  Utility::translateOpaqueConfig(typed_config, ProtobufMessage::getStrictValidationVisitor(), out);

  EXPECT_THAT(out, ProtoEq(untyped_struct));
}

// Verify that TypedStruct can be translated into an arbitrary message of correct type
// (v2 API, no upgrading).
TYPED_TEST(UtilityTypedStructTest, TypedStructToClusterV2) {
  ProtobufWkt::Any typed_config;
  API_NO_BOOST(envoy::api::v2::Cluster) cluster;
  const std::string cluster_config_yaml = R"EOF(
    drain_connections_on_host_removal: true
  )EOF";
  TestUtility::loadFromYaml(cluster_config_yaml, cluster);
  this->packTypedStructIntoAny(typed_config, cluster);

  {
    API_NO_BOOST(envoy::api::v2::Cluster) out;
    Utility::translateOpaqueConfig(typed_config, ProtobufMessage::getNullValidationVisitor(), out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
  {
    API_NO_BOOST(envoy::api::v2::Cluster) out;
    Utility::translateOpaqueConfig(typed_config, ProtobufMessage::getStrictValidationVisitor(),
                                   out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
}

// Verify that TypedStruct can be translated into an arbitrary message of correct type
// (v3 API, upgrading).
TYPED_TEST(UtilityTypedStructTest, TypedStructToClusterV3) {
  ProtobufWkt::Any typed_config;
  API_NO_BOOST(envoy::config::cluster::v3::Cluster) cluster;
  const std::string cluster_config_yaml = R"EOF(
    ignore_health_on_host_removal: true
  )EOF";
  TestUtility::loadFromYaml(cluster_config_yaml, cluster);
  this->packTypedStructIntoAny(typed_config, cluster);

  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) out;
    Utility::translateOpaqueConfig(typed_config, ProtobufMessage::getNullValidationVisitor(), out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) out;
    Utility::translateOpaqueConfig(typed_config, ProtobufMessage::getStrictValidationVisitor(),
                                   out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
}

// Verify that translation from TypedStruct into message of incorrect type fails
TYPED_TEST(UtilityTypedStructTest, TypedStructToInvalidType) {
  ProtobufWkt::Any typed_config;
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  const std::string bootstrap_config_yaml = R"EOF(
    admin:
      access_log:
      - name: envoy.access_loggers.file
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
          path: /dev/null
      address:
        pipe:
          path: "/"
  )EOF";
  TestUtility::loadFromYaml(bootstrap_config_yaml, bootstrap);
  this->packTypedStructIntoAny(typed_config, bootstrap);

  ProtobufWkt::Any out;
  EXPECT_THROW_WITH_REGEX(Utility::translateOpaqueConfig(
                              typed_config, ProtobufMessage::getStrictValidationVisitor(), out),
                          EnvoyException, "Unable to parse JSON as proto");
}

// Verify that Any can be translated into an arbitrary message of correct type
// (v2 API, no upgrading).
TEST(UtilityTest, AnyToClusterV2) {
  ProtobufWkt::Any typed_config;
  API_NO_BOOST(envoy::api::v2::Cluster) cluster;
  const std::string cluster_config_yaml = R"EOF(
    drain_connections_on_host_removal: true
  )EOF";
  TestUtility::loadFromYaml(cluster_config_yaml, cluster);
  typed_config.PackFrom(cluster);

  API_NO_BOOST(envoy::api::v2::Cluster) out;
  Utility::translateOpaqueConfig(typed_config, ProtobufMessage::getStrictValidationVisitor(), out);
  EXPECT_THAT(out, ProtoEq(cluster));
}

// Verify that Any can be translated into an arbitrary message of correct type
// (v3 API, upgrading).
TEST(UtilityTest, AnyToClusterV3) {
  ProtobufWkt::Any typed_config;
  API_NO_BOOST(envoy::config::cluster::v3::Cluster) cluster;
  const std::string cluster_config_yaml = R"EOF(
    ignore_health_on_host_removal: true
  )EOF";
  TestUtility::loadFromYaml(cluster_config_yaml, cluster);
  typed_config.PackFrom(cluster);

  API_NO_BOOST(envoy::config::cluster::v3::Cluster) out;
  Utility::translateOpaqueConfig(typed_config, ProtobufMessage::getStrictValidationVisitor(), out);
  EXPECT_THAT(out, ProtoEq(cluster));
}

// Verify that ProtobufWkt::Empty can load into a typed factory with an empty config proto
TEST(UtilityTest, EmptyToEmptyConfig) {
  ProtobufWkt::Any typed_config;
  ProtobufWkt::Empty empty_config;
  typed_config.PackFrom(empty_config);

  envoy::extensions::filters::http::cors::v3::Cors out;
  Utility::translateOpaqueConfig(typed_config, ProtobufMessage::getStrictValidationVisitor(), out);
  EXPECT_THAT(out, ProtoEq(envoy::extensions::filters::http::cors::v3::Cors()));
}

TEST(CheckApiConfigSourceSubscriptionBackingClusterTest, GrpcClusterTestAcrossTypes) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  Upstream::ClusterManager::ClusterSet primary_clusters;

  // API of type GRPC
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);

  // GRPC cluster without GRPC services.
  EXPECT_THAT(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source)
          .message(),
      HasSubstr("API configs must have either a gRPC service or a cluster name defined"));

  // Non-existent cluster.
  api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo_cluster");
  EXPECT_EQ(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source)
          .message(),
      fmt::format("{} must have a statically defined non-EDS cluster: "
                  "'foo_cluster' does not exist, was added via api, or is an EDS cluster",
                  api_config_source->GetTypeName()));

  // All ok.
  primary_clusters.insert("foo_cluster");
  EXPECT_TRUE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source)
          .ok());

  // API with cluster_names set should be rejected.
  api_config_source->add_cluster_names("foo_cluster");
  EXPECT_THAT(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source)
          .message(),
      ContainsRegex(fmt::format("{}::.DELTA_.GRPC must not have a cluster name "
                                "specified:",
                                api_config_source->GetTypeName())));
}

TEST(CheckApiConfigSourceSubscriptionBackingClusterTest, RestClusterTestAcrossTypes) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  Upstream::ClusterManager::ClusterSet primary_clusters;
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::REST);

  // Non-existent cluster.
  api_config_source->add_cluster_names("foo_cluster");
  EXPECT_EQ(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source)
          .message(),
      fmt::format("{} must have a statically defined non-EDS cluster: "
                  "'foo_cluster' does not exist, was added via api, or is an EDS cluster",
                  api_config_source->GetTypeName()));

  // All ok.
  primary_clusters.insert("foo_cluster");
  EXPECT_TRUE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source)
          .ok());
}

// Validates CheckCluster functionality.
TEST(UtilityTest, CheckCluster) {
  NiceMock<Upstream::MockClusterManager> cm;

  // Validate that proper error is thrown, when cluster is not available.
  EXPECT_THAT(Utility::checkCluster("prefix", "foo", cm, false).status().message(),
              ContainsRegex("prefix: unknown cluster 'foo'"));

  // Validate that proper error is thrown, when dynamic cluster is passed when it is not expected.
  cm.initializeClusters({"foo"}, {});
  ON_CALL(*cm.active_clusters_["foo"]->info_, addedViaApi()).WillByDefault(Return(true));
  EXPECT_EQ(Utility::checkCluster("prefix", "foo", cm, false).status().message(),
            "prefix: invalid cluster 'foo': currently only "
            "static (non-CDS) clusters are supported");
  EXPECT_TRUE(Utility::checkCluster("prefix", "foo", cm, true).ok());

  // Validate that bootstrap cluster does not throw any exceptions.
  ON_CALL(*cm.active_clusters_["foo"]->info_, addedViaApi()).WillByDefault(Return(false));
  EXPECT_TRUE(Utility::checkCluster("prefix", "foo", cm, true).ok());
  EXPECT_TRUE(Utility::checkCluster("prefix", "foo", cm, false).ok());
}

// Validates getGrpcControlPlane() functionality.
TEST(UtilityTest, GetGrpcControlPlane) {
  {
    // Google gRPC.
    envoy::config::core::v3::ApiConfigSource api_config_source;
    const std::string config_yaml = R"EOF(
      api_type: GRPC
      grpc_services:
        google_grpc:
          target_uri: trafficdirector.googleapis.com
    )EOF";
    TestUtility::loadFromYaml(config_yaml, api_config_source);
    EXPECT_THAT(Utility::getGrpcControlPlane(api_config_source),
                Optional(Eq("trafficdirector.googleapis.com")));
  }
  {
    // Envoy gRPC.
    envoy::config::core::v3::ApiConfigSource api_config_source;
    const std::string config_yaml = R"EOF(
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: some_xds_cluster
    )EOF";
    TestUtility::loadFromYaml(config_yaml, api_config_source);
    EXPECT_THAT(Utility::getGrpcControlPlane(api_config_source), Optional(Eq("some_xds_cluster")));
  }
  {
    // No control plane.
    envoy::config::core::v3::ApiConfigSource api_config_source;
    const std::string config_yaml = R"EOF(
      api_type: GRPC
    )EOF";
    TestUtility::loadFromYaml(config_yaml, api_config_source);
    EXPECT_EQ(absl::nullopt, Utility::getGrpcControlPlane(api_config_source));
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
