#include "envoy/api/v2/cluster.pb.h"
#include "envoy/common/exception.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/http/cors/v3/cors.pb.h"

#include "common/common/fmt.h"
#include "common/config/api_version.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "udpa/type/v1/typed_struct.pb.h"

using testing::_;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

TEST(UtilityTest, ComputeHashedVersion) {
  EXPECT_EQ("hash_2e1472b57af294d1", Utility::computeHashedVersion("{}").first);
  EXPECT_EQ("hash_33bf00a859c4ba3f", Utility::computeHashedVersion("foo").first);
}

TEST(UtilityTest, ApiConfigSourceRefreshDelay) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  api_config_source.mutable_refresh_delay()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(1234));
  EXPECT_EQ(1234, Utility::apiConfigSourceRefreshDelay(api_config_source).count());
}

TEST(UtilityTest, ApiConfigSourceDefaultRequestTimeout) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  EXPECT_EQ(1000, Utility::apiConfigSourceRequestTimeout(api_config_source).count());
}

TEST(UtilityTest, ApiConfigSourceRequestTimeout) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  api_config_source.mutable_request_timeout()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(1234));
  EXPECT_EQ(1234, Utility::apiConfigSourceRequestTimeout(api_config_source).count());
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

TEST(UtilityTest, TranslateApiConfigSource) {
  envoy::config::core::v3::ApiConfigSource api_config_source_rest_legacy;
  Utility::translateApiConfigSource("test_rest_legacy_cluster", 10000,
                                    ApiType::get().UnsupportedRestLegacy,
                                    api_config_source_rest_legacy);
  EXPECT_EQ(
      envoy::config::core::v3::ApiConfigSource::hidden_envoy_deprecated_UNSUPPORTED_REST_LEGACY,
      api_config_source_rest_legacy.api_type());
  EXPECT_EQ(10000,
            DurationUtil::durationToMilliseconds(api_config_source_rest_legacy.refresh_delay()));
  EXPECT_EQ("test_rest_legacy_cluster", api_config_source_rest_legacy.cluster_names(0));

  envoy::config::core::v3::ApiConfigSource api_config_source_rest;
  Utility::translateApiConfigSource("test_rest_cluster", 20000, ApiType::get().Rest,
                                    api_config_source_rest);
  EXPECT_EQ(envoy::config::core::v3::ApiConfigSource::REST, api_config_source_rest.api_type());
  EXPECT_EQ(20000, DurationUtil::durationToMilliseconds(api_config_source_rest.refresh_delay()));
  EXPECT_EQ("test_rest_cluster", api_config_source_rest.cluster_names(0));

  envoy::config::core::v3::ApiConfigSource api_config_source_grpc;
  Utility::translateApiConfigSource("test_grpc_cluster", 30000, ApiType::get().Grpc,
                                    api_config_source_grpc);
  EXPECT_EQ(envoy::config::core::v3::ApiConfigSource::GRPC, api_config_source_grpc.api_type());
  EXPECT_EQ(30000, DurationUtil::durationToMilliseconds(api_config_source_grpc.refresh_delay()));
  EXPECT_EQ("test_grpc_cluster",
            api_config_source_grpc.grpc_services(0).envoy_grpc().cluster_name());
}

TEST(UtilityTest, createTagProducer) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  auto producer = Utility::createTagProducer(bootstrap);
  ASSERT(producer != nullptr);
  std::vector<Stats::Tag> tags;
  auto extracted_name = producer->produceTags("http.config_test.rq_total", tags);
  ASSERT_EQ(extracted_name, "http.rq_total");
  ASSERT_EQ(tags.size(), 1);
}

TEST(UtilityTest, CheckFilesystemSubscriptionBackingPath) {
  Api::ApiPtr api = Api::createApiForTest();

  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkFilesystemSubscriptionBackingPath("foo", *api), EnvoyException,
      "envoy::api::v2::Path must refer to an existing path in the system: 'foo' does not exist");
  std::string test_path = TestEnvironment::temporaryDirectory();
  Utility::checkFilesystemSubscriptionBackingPath(test_path, *api);
}

TEST(UtilityTest, ParseDefaultRateLimitSettings) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  const RateLimitSettings& rate_limit_settings = Utility::parseRateLimitSettings(api_config_source);
  EXPECT_EQ(false, rate_limit_settings.enabled_);
  EXPECT_EQ(100, rate_limit_settings.max_tokens_);
  EXPECT_EQ(10, rate_limit_settings.fill_rate_);
}

TEST(UtilityTest, ParseEmptyRateLimitSettings) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  api_config_source.mutable_rate_limit_settings();
  const RateLimitSettings& rate_limit_settings = Utility::parseRateLimitSettings(api_config_source);
  EXPECT_EQ(true, rate_limit_settings.enabled_);
  EXPECT_EQ(100, rate_limit_settings.max_tokens_);
  EXPECT_EQ(10, rate_limit_settings.fill_rate_);
}

TEST(UtilityTest, ParseRateLimitSettings) {
  envoy::config::core::v3::ApiConfigSource api_config_source;
  envoy::config::core::v3::RateLimitSettings* rate_limits =
      api_config_source.mutable_rate_limit_settings();
  rate_limits->mutable_max_tokens()->set_value(500);
  rate_limits->mutable_fill_rate()->set_value(4);
  const RateLimitSettings& rate_limit_settings = Utility::parseRateLimitSettings(api_config_source);
  EXPECT_EQ(true, rate_limit_settings.enabled_);
  EXPECT_EQ(500, rate_limit_settings.max_tokens_);
  EXPECT_EQ(4, rate_limit_settings.fill_rate_);
}

// TEST(UtilityTest, FactoryForGrpcApiConfigSource) should catch misconfigured
// API configs along the dimension of ApiConfigSource type.
TEST(UtilityTest, FactoryForGrpcApiConfigSource) {
  NiceMock<Grpc::MockAsyncClientManager> async_client_manager;
  Stats::MockStore scope;

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    EXPECT_THROW_WITH_REGEX(
        Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope,
                                               false),
        EnvoyException, "API configs must have either a gRPC service or a cluster name defined:");
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services();
    api_config_source.add_grpc_services();
    EXPECT_THROW_WITH_REGEX(Utility::factoryForGrpcApiConfigSource(async_client_manager,
                                                                   api_config_source, scope, false),
                            EnvoyException,
                            fmt::format("{}::.DELTA_.GRPC must have a single gRPC service "
                                        "specified:",
                                        api_config_source.GetTypeName()));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_cluster_names();
    // this also logs a warning for setting REST cluster names for a gRPC API config.
    EXPECT_THROW_WITH_REGEX(Utility::factoryForGrpcApiConfigSource(async_client_manager,
                                                                   api_config_source, scope, false),
                            EnvoyException,
                            fmt::format("{}::.DELTA_.GRPC must not have a cluster name "
                                        "specified:",
                                        api_config_source.GetTypeName()));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_cluster_names();
    api_config_source.add_cluster_names();
    EXPECT_THROW_WITH_REGEX(Utility::factoryForGrpcApiConfigSource(async_client_manager,
                                                                   api_config_source, scope, false),
                            EnvoyException,
                            fmt::format("{}::.DELTA_.GRPC must not have a cluster name "
                                        "specified:",
                                        api_config_source.GetTypeName()));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    // this also logs a warning for configuring gRPC clusters for a REST API config.
    EXPECT_THROW_WITH_REGEX(Utility::factoryForGrpcApiConfigSource(async_client_manager,
                                                                   api_config_source, scope, false),
                            EnvoyException,
                            fmt::format("{}, if not a gRPC type, must not have a gRPC service "
                                        "specified:",
                                        api_config_source.GetTypeName()));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
    api_config_source.add_cluster_names("foo");
    EXPECT_THROW_WITH_REGEX(Utility::factoryForGrpcApiConfigSource(async_client_manager,
                                                                   api_config_source, scope, false),
                            EnvoyException,
                            fmt::format("{} type must be gRPC:", api_config_source.GetTypeName()));
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    envoy::config::core::v3::GrpcService expected_grpc_service;
    expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(async_client_manager,
                factoryForGrpcService(ProtoEq(expected_grpc_service), Ref(scope), false));
    Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope, false);
  }

  {
    envoy::config::core::v3::ApiConfigSource api_config_source;
    api_config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source.add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");
    EXPECT_CALL(
        async_client_manager,
        factoryForGrpcService(ProtoEq(api_config_source.grpc_services(0)), Ref(scope), true));
    Utility::factoryForGrpcApiConfigSource(async_client_manager, api_config_source, scope, true);
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

// Validate that an opaque config of the wrong type throws during conversion.
TEST(UtilityTest, AnyWrongType) {
  ProtobufWkt::Duration source_duration;
  source_duration.set_seconds(42);
  ProtobufWkt::Any typed_config;
  typed_config.PackFrom(source_duration);
  ProtobufWkt::Timestamp out;
  EXPECT_THROW_WITH_REGEX(
      Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                     ProtobufMessage::getStrictValidationVisitor(), out),
      EnvoyException,
      R"(Unable to unpack as google.protobuf.Timestamp: \[type.googleapis.com/google.protobuf.Duration\] .*)");
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
      R"(Unable to unpack as google.protobuf.Timestamp: \[type.googleapis.com/google.protobuf.Duration\] .*)");
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

void packTypedStructIntoAny(ProtobufWkt::Any& typed_config, const Protobuf::Message& inner) {
  udpa::type::v1::TypedStruct typed_struct;
  (*typed_struct.mutable_type_url()) =
      absl::StrCat("type.googleapis.com/", inner.GetDescriptor()->full_name());
  MessageUtil::jsonConvert(inner, *typed_struct.mutable_value());
  typed_config.PackFrom(typed_struct);
}

// Verify that udpa.type.v1.TypedStruct can be translated into google.protobuf.Struct
TEST(UtilityTest, TypedStructToStruct) {
  ProtobufWkt::Any typed_config;
  ProtobufWkt::Struct untyped_struct;
  (*untyped_struct.mutable_fields())["foo"].set_string_value("bar");
  packTypedStructIntoAny(typed_config, untyped_struct);

  ProtobufWkt::Struct out;
  Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                 ProtobufMessage::getStrictValidationVisitor(), out);

  EXPECT_THAT(out, ProtoEq(untyped_struct));
}

// Verify that regular Struct can be translated into an arbitrary message of correct type
// (v2 API, no upgrading).
TEST(UtilityTest, StructToClusterV2) {
  ProtobufWkt::Any typed_config;
  API_NO_BOOST(envoy::api::v2::Cluster) cluster;
  ProtobufWkt::Struct cluster_struct;
  const std::string cluster_config_yaml = R"EOF(
    drain_connections_on_host_removal: true
  )EOF";
  TestUtility::loadFromYaml(cluster_config_yaml, cluster);
  TestUtility::loadFromYaml(cluster_config_yaml, cluster_struct);

  {
    API_NO_BOOST(envoy::api::v2::Cluster) out;
    Utility::translateOpaqueConfig({}, cluster_struct, ProtobufMessage::getNullValidationVisitor(),
                                   out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
  {
    API_NO_BOOST(envoy::api::v2::Cluster) out;
    Utility::translateOpaqueConfig({}, cluster_struct,
                                   ProtobufMessage::getStrictValidationVisitor(), out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
}

// Verify that regular Struct can be translated into an arbitrary message of correct type
// (v3 API, upgrading).
TEST(UtilityTest, StructToClusterV3) {
  ProtobufWkt::Any typed_config;
  API_NO_BOOST(envoy::config::cluster::v3::Cluster) cluster;
  ProtobufWkt::Struct cluster_struct;
  const std::string cluster_config_yaml = R"EOF(
    ignore_health_on_host_removal: true
  )EOF";
  TestUtility::loadFromYaml(cluster_config_yaml, cluster);
  TestUtility::loadFromYaml(cluster_config_yaml, cluster_struct);

  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) out;
    Utility::translateOpaqueConfig({}, cluster_struct, ProtobufMessage::getNullValidationVisitor(),
                                   out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) out;
    Utility::translateOpaqueConfig({}, cluster_struct,
                                   ProtobufMessage::getStrictValidationVisitor(), out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
}

// Verify that udpa.type.v1.TypedStruct can be translated into an arbitrary message of correct type
// (v2 API, no upgrading).
TEST(UtilityTest, TypedStructToClusterV2) {
  ProtobufWkt::Any typed_config;
  API_NO_BOOST(envoy::api::v2::Cluster) cluster;
  const std::string cluster_config_yaml = R"EOF(
    drain_connections_on_host_removal: true
  )EOF";
  TestUtility::loadFromYaml(cluster_config_yaml, cluster);
  packTypedStructIntoAny(typed_config, cluster);

  {
    API_NO_BOOST(envoy::api::v2::Cluster) out;
    Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                   ProtobufMessage::getNullValidationVisitor(), out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
  {
    API_NO_BOOST(envoy::api::v2::Cluster) out;
    Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                   ProtobufMessage::getStrictValidationVisitor(), out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
}

// Verify that udpa.type.v1.TypedStruct can be translated into an arbitrary message of correct type
// (v3 API, upgrading).
TEST(UtilityTest, TypedStructToClusterV3) {
  ProtobufWkt::Any typed_config;
  API_NO_BOOST(envoy::config::cluster::v3::Cluster) cluster;
  const std::string cluster_config_yaml = R"EOF(
    ignore_health_on_host_removal: true
  )EOF";
  TestUtility::loadFromYaml(cluster_config_yaml, cluster);
  packTypedStructIntoAny(typed_config, cluster);

  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) out;
    Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                   ProtobufMessage::getNullValidationVisitor(), out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
  {
    API_NO_BOOST(envoy::config::cluster::v3::Cluster) out;
    Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                   ProtobufMessage::getStrictValidationVisitor(), out);
    EXPECT_THAT(out, ProtoEq(cluster));
  }
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
  Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                 ProtobufMessage::getStrictValidationVisitor(), out);
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
  Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                 ProtobufMessage::getStrictValidationVisitor(), out);
  EXPECT_THAT(out, ProtoEq(cluster));
}

// Verify that translation from udpa.type.v1.TypedStruct into message of incorrect type fails
TEST(UtilityTest, TypedStructToInvalidType) {
  ProtobufWkt::Any typed_config;
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  const std::string bootstrap_config_yaml = R"EOF(
    admin:
      access_log_path: /dev/null
      address:
        pipe:
          path: "/"
  )EOF";
  TestUtility::loadFromYaml(bootstrap_config_yaml, bootstrap);
  packTypedStructIntoAny(typed_config, bootstrap);

  ProtobufWkt::Any out;
  EXPECT_THROW_WITH_REGEX(
      Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                     ProtobufMessage::getStrictValidationVisitor(), out),
      EnvoyException, "Unable to parse JSON as proto");
}

// Verify that ProtobufWkt::Empty can load into a typed factory with an empty config proto
TEST(UtilityTest, EmptyToEmptyConfig) {
  ProtobufWkt::Any typed_config;
  ProtobufWkt::Empty empty_config;
  typed_config.PackFrom(empty_config);

  envoy::extensions::filters::http::cors::v3::Cors out;
  Utility::translateOpaqueConfig(typed_config, ProtobufWkt::Struct(),
                                 ProtobufMessage::getStrictValidationVisitor(), out);
  EXPECT_THAT(out, ProtoEq(envoy::extensions::filters::http::cors::v3::Cors()));
}

TEST(CheckApiConfigSourceSubscriptionBackingClusterTest, GrpcClusterTestAcrossTypes) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  Upstream::ClusterManager::ClusterSet primary_clusters;

  // API of type GRPC
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);

  // GRPC cluster without GRPC services.
  EXPECT_THROW_WITH_REGEX(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source),
      EnvoyException, "API configs must have either a gRPC service or a cluster name defined:");

  // Non-existent cluster.
  api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo_cluster");
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source),
      EnvoyException,
      fmt::format("{} must have a statically defined non-EDS cluster: "
                  "'foo_cluster' does not exist, was added via api, or is an EDS cluster",
                  api_config_source->GetTypeName()));

  // All ok.
  primary_clusters.insert("foo_cluster");
  Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source);

  // API with cluster_names set should be rejected.
  api_config_source->add_cluster_names("foo_cluster");
  EXPECT_THROW_WITH_REGEX(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source),
      EnvoyException,
      fmt::format("{}::.DELTA_.GRPC must not have a cluster name "
                  "specified:",
                  api_config_source->GetTypeName()));
}

TEST(CheckApiConfigSourceSubscriptionBackingClusterTest, RestClusterTestAcrossTypes) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  Upstream::ClusterManager::ClusterSet primary_clusters;
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::REST);

  // Non-existent cluster.
  api_config_source->add_cluster_names("foo_cluster");
  EXPECT_THROW_WITH_MESSAGE(
      Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source),
      EnvoyException,
      fmt::format("{} must have a statically defined non-EDS cluster: "
                  "'foo_cluster' does not exist, was added via api, or is an EDS cluster",
                  api_config_source->GetTypeName()));

  // All ok.
  primary_clusters.insert("foo_cluster");
  Utility::checkApiConfigSourceSubscriptionBackingCluster(primary_clusters, *api_config_source);
}

// Validates CheckCluster functionality.
TEST(UtilityTest, CheckCluster) {
  Upstream::MockClusterManager cm;

  // Validate that proper error is thrown, when cluster is not available.
  EXPECT_CALL(cm, get("foo")).WillOnce(Return(nullptr));
  EXPECT_THROW_WITH_MESSAGE(Utility::checkCluster("prefix", "foo", cm, false), EnvoyException,
                            "prefix: unknown cluster 'foo'");

  // Validate that proper error is thrown, when dynamic cluster is passed when it is not expected.
  NiceMock<Upstream::MockThreadLocalCluster> api_cluster;
  EXPECT_CALL(cm, get("foo")).Times(2).WillRepeatedly(Return(&api_cluster));
  EXPECT_CALL(api_cluster, info());
  EXPECT_CALL(*api_cluster.cluster_.info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(Utility::checkCluster("prefix", "foo", cm, false), EnvoyException,
                            "prefix: invalid cluster 'foo': currently only "
                            "static (non-CDS) clusters are supported");
  EXPECT_NO_THROW(Utility::checkCluster("prefix", "foo", cm, true));

  // Validate that bootstrap cluster does not throw any exceptions.
  NiceMock<Upstream::MockThreadLocalCluster> bootstrap_cluster;
  EXPECT_CALL(cm, get("foo")).Times(2).WillRepeatedly(Return(&bootstrap_cluster));
  EXPECT_NO_THROW(Utility::checkCluster("prefix", "foo", cm, true));
  EXPECT_NO_THROW(Utility::checkCluster("prefix", "foo", cm, false));
}

} // namespace
} // namespace Config
} // namespace Envoy
