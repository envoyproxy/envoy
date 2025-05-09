#include "envoy/config/config_validator.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/registry/registry.h"

#include "source/common/config/xds_manager_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

using ::Envoy::StatusHelpers::StatusCodeIs;
using testing::ByMove;
using testing::HasSubstr;
using testing::Return;
using testing::ReturnRef;

// A gRPC MuxFactory that returns a MockGrpcMux instance when trying to instantiate a mux for the
// `envoy.config_mux.grpc_mux_factory` type. This enables testing call expectations on the ADS gRPC
// mux.
class MockGrpcMuxFactory : public MuxFactory {
public:
  MockGrpcMuxFactory() {
    ON_CALL(*this, create(_, _, _, _, _, _, _, _, _, _, _, _))
        .WillByDefault(Invoke(
            [](std::unique_ptr<Grpc::RawAsyncClient>&&, std::unique_ptr<Grpc::RawAsyncClient>&&,
               Event::Dispatcher&, Random::RandomGenerator&, Stats::Scope&,
               const envoy::config::core::v3::ApiConfigSource&, const LocalInfo::LocalInfo&,
               std::unique_ptr<Config::CustomConfigValidators>&&, BackOffStrategyPtr&&,
               OptRef<Config::XdsConfigTracker>, OptRef<Config::XdsResourcesDelegate>,
               bool) -> std::shared_ptr<Config::GrpcMux> {
              return std::make_shared<NiceMock<MockGrpcMux>>();
            }));
  }

  std::string name() const override { return "envoy.config_mux.grpc_mux_factory"; }
  void shutdownAll() override {}

  MOCK_METHOD(std::shared_ptr<Config::GrpcMux>, create,
              (std::unique_ptr<Grpc::RawAsyncClient>&&, std::unique_ptr<Grpc::RawAsyncClient>&&,
               Event::Dispatcher&, Random::RandomGenerator&, Stats::Scope&,
               const envoy::config::core::v3::ApiConfigSource&, const LocalInfo::LocalInfo&,
               std::unique_ptr<Config::CustomConfigValidators>&&, BackOffStrategyPtr&&,
               OptRef<Config::XdsConfigTracker>, OptRef<Config::XdsResourcesDelegate>, bool));
};

// A fake cluster validator that exercises the code that uses ADS with
// config_validators.
class FakeConfigValidatorFactory : public Config::ConfigValidatorFactory {
public:
  FakeConfigValidatorFactory() = default;

  Config::ConfigValidatorPtr createConfigValidator(const ProtobufWkt::Any&,
                                                   ProtobufMessage::ValidationVisitor&) override {
    return nullptr;
  }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Value instead of a custom empty config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Value()};
  }

  std::string name() const override { return "envoy.fake_validator"; }

  std::string typeUrl() const override {
    return "type.googleapis.com/envoy.fake_validator.v3.FakeValidator";
  }
};

class XdsManagerImplTest : public testing::Test {
public:
  XdsManagerImplTest()
      : xds_manager_impl_(dispatcher_, api_, stats_, local_info_, validation_context_, server_) {
    ON_CALL(validation_context_, staticValidationVisitor())
        .WillByDefault(ReturnRef(validation_visitor_));
  }

  void initialize(const std::string& bootstrap_yaml = "") {
    if (!bootstrap_yaml.empty()) {
      TestUtility::loadFromYaml(bootstrap_yaml, server_.bootstrap_);
    }
    ASSERT_OK(xds_manager_impl_.initialize(server_.bootstrap_, &cm_));
  }

  NiceMock<Server::MockInstance> server_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_ = server_context_.store_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Api::MockApi> api_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  XdsManagerImpl xds_manager_impl_;
};

// Validates that a call to shutdown succeeds.
TEST_F(XdsManagerImplTest, ShutdownSuccessful) {
  initialize();
  xds_manager_impl_.shutdown();
}

// Validates that ADS replacement fails when ADS isn't configured.
TEST_F(XdsManagerImplTest, AdsReplacementNoPriorAdsRejection) {
  // Make the server return a bootstrap that returns a non-ADS config.
  initialize(R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: cluster_1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
 )EOF");

  // Create the ADS config to replace the ADS.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: cluster_1
  )EOF",
                            new_ads_config);

  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInternal));
  EXPECT_EQ(res.message(),
            "Cannot replace an ADS config when one wasn't previously configured in the bootstrap");
}

// Validates that ADS replacement with primary source only works.
TEST_F(XdsManagerImplTest, AdsReplacementPrimaryOnly) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.xds_failover_support", "true"}});
  testing::InSequence s;
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<Config::MuxFactory> registry(factory);
  // Replace the created GrpcMux mock.
  std::shared_ptr<NiceMock<MockGrpcMux>> ads_mux_shared(std::make_shared<NiceMock<MockGrpcMux>>());
  NiceMock<Config::MockGrpcMux>& ads_mux(*ads_mux_shared.get());
  EXPECT_CALL(factory, create(_, _, _, _, _, _, _, _, _, _, _, _))
      .WillOnce(Invoke(
          [&ads_mux_shared](std::unique_ptr<Grpc::RawAsyncClient>&& primary_async_client,
                            std::unique_ptr<Grpc::RawAsyncClient>&& failover_async_client,
                            Event::Dispatcher&, Random::RandomGenerator&, Stats::Scope&,
                            const envoy::config::core::v3::ApiConfigSource&,
                            const LocalInfo::LocalInfo&,
                            std::unique_ptr<Config::CustomConfigValidators>&&, BackOffStrategyPtr&&,
                            OptRef<Config::XdsConfigTracker>, OptRef<Config::XdsResourcesDelegate>,
                            bool) -> std::shared_ptr<Config::GrpcMux> {
            EXPECT_NE(primary_async_client, nullptr);
            EXPECT_EQ(failover_async_client, nullptr);
            return ads_mux_shared;
          }));

  // Start with 2 static clusters, and set the first to be the ADS cluster.
  initialize(R"EOF(
  dynamic_resources:
    ads_config:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster1
  static_resources:
    clusters:
    - name: ads_cluster1
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: ads_cluster1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: ads_cluster2
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: ads_cluster2
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  // Replace the ADS config to be the second ADS cluster.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster2
  )EOF",
                            new_ads_config);

  Grpc::RawAsyncClientPtr failover_client;
  EXPECT_CALL(ads_mux, updateMuxSource(_, _, _, _, ProtoEq(new_ads_config)))
      .WillOnce(Invoke([](Grpc::RawAsyncClientPtr&& primary_async_client,
                          Grpc::RawAsyncClientPtr&& failover_async_client, Stats::Scope&,
                          BackOffStrategyPtr&&,
                          const envoy::config::core::v3::ApiConfigSource&) -> absl::Status {
        EXPECT_NE(primary_async_client, nullptr);
        EXPECT_EQ(failover_async_client, nullptr);
        return absl::OkStatus();
      }));
  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_TRUE(res.ok());
}

// Validates that ADS replacement with primary and failover sources works.
TEST_F(XdsManagerImplTest, AdsReplacementPrimaryAndFailover) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.xds_failover_support", "true"}});
  testing::InSequence s;
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<Config::MuxFactory> registry(factory);
  // Replace the created GrpcMux mock.
  std::shared_ptr<NiceMock<Config::MockGrpcMux>> ads_mux_shared(
      std::make_shared<NiceMock<Config::MockGrpcMux>>());
  NiceMock<Config::MockGrpcMux>& ads_mux(*ads_mux_shared.get());
  EXPECT_CALL(factory, create(_, _, _, _, _, _, _, _, _, _, _, _))
      .WillOnce(Invoke(
          [&ads_mux_shared](std::unique_ptr<Grpc::RawAsyncClient>&& primary_async_client,
                            std::unique_ptr<Grpc::RawAsyncClient>&& failover_async_client,
                            Event::Dispatcher&, Random::RandomGenerator&, Stats::Scope&,
                            const envoy::config::core::v3::ApiConfigSource&,
                            const LocalInfo::LocalInfo&,
                            std::unique_ptr<Config::CustomConfigValidators>&&, BackOffStrategyPtr&&,
                            OptRef<Config::XdsConfigTracker>, OptRef<Config::XdsResourcesDelegate>,
                            bool) -> std::shared_ptr<Config::GrpcMux> {
            EXPECT_NE(primary_async_client, nullptr);
            EXPECT_NE(failover_async_client, nullptr);
            return ads_mux_shared;
          }));

  // Start with 2 static clusters, and set the first to be the ADS cluster.
  initialize(R"EOF(
  dynamic_resources:
    ads_config:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
      - envoy_grpc:
          cluster_name: primary_ads_cluster
      - envoy_grpc:
          cluster_name: failover_ads_cluster
  static_resources:
    clusters:
    - name: primary_ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: primary_ads_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: failover_ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: failover_ads_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  // Replace the ADS config to be the second ADS cluster.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
      - envoy_grpc:
          cluster_name: primary_ads_cluster
      - envoy_grpc:
          cluster_name: failover_ads_cluster
  )EOF",
                            new_ads_config);

  Grpc::RawAsyncClientPtr failover_client;
  EXPECT_CALL(ads_mux, updateMuxSource(_, _, _, _, ProtoEq(new_ads_config)))
      .WillOnce(Invoke([](Grpc::RawAsyncClientPtr&& primary_async_client,
                          Grpc::RawAsyncClientPtr&& failover_async_client, Stats::Scope&,
                          BackOffStrategyPtr&&,
                          const envoy::config::core::v3::ApiConfigSource&) -> absl::Status {
        EXPECT_NE(primary_async_client, nullptr);
        EXPECT_NE(failover_async_client, nullptr);
        return absl::OkStatus();
      }));
  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_TRUE(res.ok());
}

// Validates that setAdsConfigSource validation failure is detected.
TEST_F(XdsManagerImplTest, AdsReplacementInvalidConfig) {
  testing::InSequence s;
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<MuxFactory> registry(factory);

  // Start with a single static cluster, and set it to be the ADS cluster.
  initialize(R"EOF(
  dynamic_resources:
    ads_config:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
  static_resources:
    clusters:
    - name: ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: ads_cluster1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  envoy::config::core::v3::ApiConfigSource new_ads_config;
  new_ads_config.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  // Add an empty gRPC service (without EnvoyGrpc/GoogleGrpc) which should be
  // invalid.
  new_ads_config.add_grpc_services();
  absl::Status res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInternal));
  EXPECT_THAT(res.message(), HasSubstr("Proto constraint validation failed"));
}

// Validates that ADS replacement with unknown cluster fails.
TEST_F(XdsManagerImplTest, AdsReplacementUnknownCluster) {
  testing::InSequence s;
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<MuxFactory> registry(factory);

  // Start with a single static cluster, and set it to be the ADS cluster.
  initialize(R"EOF(
  dynamic_resources:
    ads_config:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
  static_resources:
    clusters:
    - name: ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: ads_cluster1
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  // Replace the ADS config to be the second ADS cluster.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster2
  )EOF",
                            new_ads_config);

  // Emulates an error for gRPC-cluster not found.
  EXPECT_CALL(cm_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(
          Return(ByMove(absl::InvalidArgumentError("Unknown gRPC client cluster 'ads_cluster2'"))));
  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(res.message(), "Unknown gRPC client cluster 'ads_cluster2'");
}

// Validates that ADS replacement with unknown failover cluster fails.
TEST_F(XdsManagerImplTest, AdsReplacementUnknownFailoverCluster) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.xds_failover_support", "true"}});
  testing::InSequence s;
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<MuxFactory> registry(factory);

  // Start with 2 static clusters, and set the first to be the ADS cluster, and
  // the second as failover.
  initialize(R"EOF(
  dynamic_resources:
    ads_config:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
      - envoy_grpc:
          cluster_name: primary_ads_cluster
      - envoy_grpc:
          cluster_name: failover_ads_cluster
  static_resources:
    clusters:
    - name: primary_ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: primary_ads_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: failover_ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: failover_ads_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  // Replace the ADS config to be the second ADS cluster.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
      - envoy_grpc:
          cluster_name: primary_ads_cluster
      - envoy_grpc:
          cluster_name: non_existent_failover_ads_cluster
  )EOF",
                            new_ads_config);

  // Emulates a successful finding of the primary_ads_cluster.
  envoy::config::core::v3::GrpcService expected_primary_grpc_service;
  expected_primary_grpc_service.mutable_envoy_grpc()->set_cluster_name("primary_ads_cluster");
  EXPECT_CALL(cm_.async_client_manager_,
              factoryForGrpcService(ProtoEq(expected_primary_grpc_service), _, _))
      .WillOnce(Return(ByMove(std::make_unique<Grpc::MockAsyncClientFactory>())));
  // Emulates an error for non_existent_failover_ads_cluster not found.
  envoy::config::core::v3::GrpcService expected_failover_grpc_service;
  expected_failover_grpc_service.mutable_envoy_grpc()->set_cluster_name(
      "non_existent_failover_ads_cluster");
  EXPECT_CALL(cm_.async_client_manager_,
              factoryForGrpcService(ProtoEq(expected_failover_grpc_service), _, _))
      .WillOnce(Return(ByMove(absl::InvalidArgumentError(
          "Unknown gRPC client cluster 'non_existent_failover_ads_cluster'"))));
  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(res.message(), "Unknown gRPC client cluster 'non_existent_failover_ads_cluster'");
}

// Validates that ADS replacement fails when ADS type is different (SotW <-> Delta).
TEST_F(XdsManagerImplTest, AdsReplacementDifferentAdsTypeRejection) {
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<MuxFactory> registry(factory);

  // Change between SotW -> Delta-xDS, and see that it fails.
  initialize(R"EOF(
    dynamic_resources:
      ads_config:
        api_type: GRPC
        set_node_on_first_message_only: true
        grpc_services:
          envoy_grpc:
            cluster_name: ads_cluster
    static_resources:
      clusters:
      - name: ads_cluster
        connect_timeout: 0.250s
        type: static
        lb_policy: round_robin
        load_assignment:
          cluster_name: ads_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  // Replace the ADS config to be a delta-xDS one.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: DELTA_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
  )EOF",
                            new_ads_config);
  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInternal));
  EXPECT_EQ(res.message(),
            "Cannot replace an ADS config with a different api_type (expected: GRPC)");
}

// Validates that ADS replacement fails when a wrong backoff strategy is used.
TEST_F(XdsManagerImplTest, AdsReplacementInvalidBackoffRejection) {
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<Config::MuxFactory> registry(factory);

  initialize(R"EOF(
  dynamic_resources:
    ads_config:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
  static_resources:
    clusters:
    - name: ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: ads_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  // Replace the ADS config with an invalid backoff strategy.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
          retry_policy:
            retry_back_off:
              base_interval: 100s
              max_interval: 10s
  )EOF",
                            new_ads_config);
  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(res.message(), "max_interval must be greater than or equal to the base_interval");
}

// Validates that ADS replacement of unsupported API type is rejected.
TEST_F(XdsManagerImplTest, AdsReplacementUnsupportedTypeRejection) {
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<Config::MuxFactory> registry(factory);

  // Use GRPC type which is supported, but make sure that it will return
  // AGGREGATED_GRPC when invoking replaceAdsMux() to emulate a type which is not
  // supported.
  initialize(R"EOF(
  dynamic_resources:
    ads_config:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
  static_resources:
    clusters:
    - name: ads_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: ads_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  // Replace the ADS config with an unsupported API type.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
  )EOF",
                            new_ads_config);
  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInternal));
  EXPECT_EQ(res.message(),
            "Cannot replace an ADS config with a different api_type (expected: GRPC)");
}

// Validates that ADS replacement fails when there are a different number of custom validators
// defined between the original ADS config and the replacement.
TEST_F(XdsManagerImplTest, AdsReplacementNumberOfCustomValidatorsRejection) {
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<MuxFactory> registry(factory);
  FakeConfigValidatorFactory fake_config_validator_factory;
  Registry::InjectFactory<ConfigValidatorFactory> registry2(fake_config_validator_factory);

  initialize(R"EOF(
    dynamic_resources:
      ads_config:
        api_type: GRPC
        set_node_on_first_message_only: true
        grpc_services:
          envoy_grpc:
            cluster_name: ads_cluster
        config_validators:
        - name: envoy.fake_validator
          typed_config:
            "@type": type.googleapis.com/google.protobuf.Value
    static_resources:
      clusters:
      - name: ads_cluster
        connect_timeout: 0.250s
        type: static
        lb_policy: round_robin
        load_assignment:
          cluster_name: ads_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  // Replace the ADS config to be a delta-xDS one.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
  )EOF",
                            new_ads_config);
  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInternal));
  EXPECT_THAT(res.message(),
              HasSubstr("Cannot replace config_validators in ADS config (different size)"));
}

// Validates that ADS replacement fails when a custom validators with some
// different contents is used compared to the original ADS config.
TEST_F(XdsManagerImplTest, AdsReplacementContentsOfCustomValidatorsRejection) {
  NiceMock<MockGrpcMuxFactory> factory;
  Registry::InjectFactory<Config::MuxFactory> registry(factory);
  FakeConfigValidatorFactory fake_config_validator_factory;
  Registry::InjectFactory<ConfigValidatorFactory> registry2(fake_config_validator_factory);

  initialize(R"EOF(
    dynamic_resources:
      ads_config:
        api_type: GRPC
        set_node_on_first_message_only: true
        grpc_services:
          envoy_grpc:
            cluster_name: ads_cluster
        config_validators:
        - name: envoy.fake_validator
          typed_config:
            "@type": type.googleapis.com/google.protobuf.Value
            value:
              number_value: 5.0
    static_resources:
      clusters:
      - name: ads_cluster
        connect_timeout: 0.250s
        type: static
        lb_policy: round_robin
        load_assignment:
          cluster_name: ads_cluster
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  // Replace the ADS config to be a delta-xDS one.
  envoy::config::core::v3::ApiConfigSource new_ads_config;
  TestUtility::loadFromYaml(R"EOF(
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: ads_cluster
      config_validators:
      - name: envoy.fake_validator
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Value
          value:
            number_value: 7.0
  )EOF",
                            new_ads_config);
  const auto res = xds_manager_impl_.setAdsConfigSource(new_ads_config);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInternal));
  EXPECT_THAT(res.message(),
              HasSubstr("Cannot replace config_validators in ADS config (different contents)"));
}

} // namespace
} // namespace Config
} // namespace Envoy
