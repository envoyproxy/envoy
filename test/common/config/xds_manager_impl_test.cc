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
  MockGrpcMuxFactory(absl::string_view name = "envoy.config_mux.grpc_mux_factory") : name_(name) {
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

  std::string name() const override { return name_; }
  void shutdownAll() override {}

  MOCK_METHOD(std::shared_ptr<Config::GrpcMux>, create,
              (std::unique_ptr<Grpc::RawAsyncClient>&&, std::unique_ptr<Grpc::RawAsyncClient>&&,
               Event::Dispatcher&, Random::RandomGenerator&, Stats::Scope&,
               const envoy::config::core::v3::ApiConfigSource&, const LocalInfo::LocalInfo&,
               std::unique_ptr<Config::CustomConfigValidators>&&, BackOffStrategyPtr&&,
               OptRef<Config::XdsConfigTracker>, OptRef<Config::XdsResourcesDelegate>, bool));
  const std::string name_;
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

// A ConfigSubscriptionFactory for the xDS-TP based config-sources.
// Returns a MockSubscriptionFactory instance when trying to instantiate a mux for the
// `envoy.config_subscription.ads` type.
class MockAdsConfigSubscriptionFactory : public ConfigSubscriptionFactory {
public:
  std::string name() const override { return "envoy.config_subscription.ads"; }
  MOCK_METHOD(Config::SubscriptionPtr, create, (SubscriptionData & data), (override));
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

/*
 * Tests that cover the usage of xDS-TP based configuration-sources.
 */
class XdsManagerImplXdstpConfigSourcesTest : public testing::Test {
public:
  XdsManagerImplXdstpConfigSourcesTest()
      : grpc_mux_registry_(grpc_mux_factory_),
        xds_manager_impl_(dispatcher_, api_, stats_, local_info_, validation_context_, server_) {
    ON_CALL(validation_context_, staticValidationVisitor())
        .WillByDefault(ReturnRef(validation_visitor_));
  }

  void initialize(const std::string& bootstrap_yaml = "", bool enable_authority_a = false,
                  bool enable_authority_b = false, bool enable_default_authority = false) {
    if (!bootstrap_yaml.empty()) {
      TestUtility::loadFromYaml(bootstrap_yaml, server_.bootstrap_);
    }

    if (enable_authority_a) {
      EXPECT_CALL(grpc_mux_factory_, create(_, _, _, _, _, _, _, _, _, _, _, _))
          .WillOnce(Invoke(
              [&](std::unique_ptr<Grpc::RawAsyncClient>&& primary_async_client,
                  std::unique_ptr<Grpc::RawAsyncClient>&&, Event::Dispatcher&,
                  Random::RandomGenerator&, Stats::Scope&,
                  const envoy::config::core::v3::ApiConfigSource&, const LocalInfo::LocalInfo&,
                  std::unique_ptr<Config::CustomConfigValidators>&&, BackOffStrategyPtr&&,
                  OptRef<Config::XdsConfigTracker>, OptRef<Config::XdsResourcesDelegate>,
                  bool) -> std::shared_ptr<Config::GrpcMux> {
                EXPECT_NE(primary_async_client, nullptr);
                return authority_A_mux_;
              }));
    }
    if (enable_authority_b) {
      EXPECT_CALL(grpc_mux_factory_, create(_, _, _, _, _, _, _, _, _, _, _, _))
          .WillOnce(Invoke(
              [&](std::unique_ptr<Grpc::RawAsyncClient>&& primary_async_client,
                  std::unique_ptr<Grpc::RawAsyncClient>&&, Event::Dispatcher&,
                  Random::RandomGenerator&, Stats::Scope&,
                  const envoy::config::core::v3::ApiConfigSource&, const LocalInfo::LocalInfo&,
                  std::unique_ptr<Config::CustomConfigValidators>&&, BackOffStrategyPtr&&,
                  OptRef<Config::XdsConfigTracker>, OptRef<Config::XdsResourcesDelegate>,
                  bool) -> std::shared_ptr<Config::GrpcMux> {
                EXPECT_NE(primary_async_client, nullptr);
                return authority_B_mux_;
              }));
    }
    if (enable_default_authority) {
      EXPECT_CALL(grpc_mux_factory_, create(_, _, _, _, _, _, _, _, _, _, _, _))
          .WillOnce(Invoke(
              [&](std::unique_ptr<Grpc::RawAsyncClient>&& primary_async_client,
                  std::unique_ptr<Grpc::RawAsyncClient>&&, Event::Dispatcher&,
                  Random::RandomGenerator&, Stats::Scope&,
                  const envoy::config::core::v3::ApiConfigSource&, const LocalInfo::LocalInfo&,
                  std::unique_ptr<Config::CustomConfigValidators>&&, BackOffStrategyPtr&&,
                  OptRef<Config::XdsConfigTracker>, OptRef<Config::XdsResourcesDelegate>,
                  bool) -> std::shared_ptr<Config::GrpcMux> {
                EXPECT_NE(primary_async_client, nullptr);
                return default_mux_;
              }));
    }

    ASSERT_OK(xds_manager_impl_.initialize(server_.bootstrap_, &cm_));
    if (enable_authority_a || enable_authority_b || enable_default_authority) {
      ASSERT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));
    }
  }

  NiceMock<MockGrpcMuxFactory> grpc_mux_factory_;
  Registry::InjectFactory<MuxFactory> grpc_mux_registry_;
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
  std::shared_ptr<NiceMock<MockGrpcMux>> default_mux_{std::make_shared<NiceMock<MockGrpcMux>>()};
  std::shared_ptr<NiceMock<MockGrpcMux>> authority_A_mux_{
      std::make_shared<NiceMock<MockGrpcMux>>()};
  std::shared_ptr<NiceMock<MockGrpcMux>> authority_B_mux_{
      std::make_shared<NiceMock<MockGrpcMux>>()};
};

// Validates that when only a default config source defined with no authority, a gRPC connection is
// established.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultConfigSourceNoAuthority) {
  testing::InSequence s;
  // Have a single default_config_source with no authorities in it.
  initialize(R"EOF(
  default_config_source:
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF",
             false, false, true);
}

// Validates that when a default config source that is not gRPC based is
// rejected as this is currently not supported.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultConfigSourceSotwNonGrpc) {
  // Temporarily remove the config mux factories.
  auto saved_factories = Registry::FactoryRegistry<MuxFactory>::factories();
  Registry::FactoryRegistry<MuxFactory>::factories().clear();
  Registry::InjectFactory<MuxFactory>::resetTypeMappings();
  // Have a single default_config_source configured with ADS.
  initialize(R"EOF(
  default_config_source:
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  const auto res = xds_manager_impl_.initializeAdsConnections(server_.bootstrap_);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(res.message(), HasSubstr("envoy.config_mux.grpc_mux_factory not found"));
  // Restore the mux factories.
  Registry::FactoryRegistry<MuxFactory>::factories() = saved_factories;
  Registry::InjectFactory<MuxFactory>::resetTypeMappings();
}

// Validates that when a default config source that is not gRPC based is
// rejected as this is currently not supported.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultConfigSourceDeltaNonGrpc) {
  // Temporarily remove the config mux factories.
  auto saved_factories = Registry::FactoryRegistry<MuxFactory>::factories();
  Registry::FactoryRegistry<MuxFactory>::factories().clear();
  Registry::InjectFactory<MuxFactory>::resetTypeMappings();
  // Have a single default_config_source configured with ADS.
  initialize(R"EOF(
  default_config_source:
    api_config_source:
      api_type: AGGREGATED_DELTA_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  const auto res = xds_manager_impl_.initializeAdsConnections(server_.bootstrap_);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(res.message(), HasSubstr("envoy.config_mux.new_grpc_mux_factory not found"));
  // Restore the mux factories.
  Registry::FactoryRegistry<MuxFactory>::factories() = saved_factories;
  Registry::InjectFactory<MuxFactory>::resetTypeMappings();
}

// Validates that a default config source is used but the gRPC extension
// isn't linked, the config is rejected.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultConfigSourceNoExtensionLinked) {
  // Have a single default_config_source configured with ADS.
  initialize(R"EOF(
  default_config_source:
    ads: {}
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  const auto res = xds_manager_impl_.initializeAdsConnections(server_.bootstrap_);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      res.message(),
      HasSubstr(
          "Only api_config_source type is currently supported for xdstp-based config sources."));
}

// Test only a default config source defined with two authorities.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultConfigSourceTwoAuthorities) {
  testing::InSequence s;
  // Have a single default_config_source with two authorities in it.
  initialize(R"EOF(
  default_config_source:
    authorities:
    - name: authority_D1.com
    - name: authority_D2.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF",
             false, false, true);
  ////EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));
}

// Test only a default config source with wrong api_type.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultConfigSourceWrongApi) {
  testing::InSequence s;
  // Have a single default_config_source with non-aggregated api type.
  initialize(R"EOF(
  default_config_source:
    authorities:
    - name: authority_D1.com
    api_config_source:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  const auto res = xds_manager_impl_.initializeAdsConnections(server_.bootstrap_);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(res.message(), HasSubstr("xdstp-based config source authority only supports "
                                       "AGGREGATED_GRPC and AGGREGATED_DELTA_GRPC types."));
}

// Test a non-default only config source with repeated authority (invalid).
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultConfigSourceRepeatedAuthority) {
  testing::InSequence s;
  // Have a single default_config_source with repeated authority_D1.com in it.
  initialize(R"EOF(
  default_config_source:
    authorities:
    - name: authority_D1.com
    - name: authority_D1.com
    api_config_source:
      api_type: GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  const auto res = xds_manager_impl_.initializeAdsConnections(server_.bootstrap_);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(res.message(),
              HasSubstr("xdstp-based config source authority authority_D1.com is configured more "
                        "than once in an xdstp-based config source."));
}

// Test only a non-default config source defined with no authority (should fail).
TEST_F(XdsManagerImplXdstpConfigSourcesTest, NonDefaultConfigSourceNoAuthority) {
  testing::InSequence s;
  // Have a single config_source with no authorities in it.
  initialize(R"EOF(
  config_sources:
  - api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source1_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source1_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  const auto res = xds_manager_impl_.initializeAdsConnections(server_.bootstrap_);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(res.message(),
              HasSubstr("xdstp-based non-default config source must have at least one authority."));
}

// Test only a non-default config source with an authority using AGGREGATED_DELTA_GRPC.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, NonDefaultConfigSourceDeltaGrpc) {
  testing::InSequence s;
  // Replace the created GrpcMux mock with a delta-xDS one.
  NiceMock<MockGrpcMuxFactory> factory("envoy.config_mux.new_grpc_mux_factory");
  Registry::InjectFactory<Config::MuxFactory> registry(factory);
  EXPECT_CALL(factory, create(_, _, _, _, _, _, _, _, _, _, _, _))
      .WillOnce(Invoke(
          [&](std::unique_ptr<Grpc::RawAsyncClient>&& primary_async_client,
              std::unique_ptr<Grpc::RawAsyncClient>&&, Event::Dispatcher&, Random::RandomGenerator&,
              Stats::Scope&, const envoy::config::core::v3::ApiConfigSource&,
              const LocalInfo::LocalInfo&, std::unique_ptr<Config::CustomConfigValidators>&&,
              BackOffStrategyPtr&&, OptRef<Config::XdsConfigTracker>,
              OptRef<Config::XdsResourcesDelegate>, bool) -> std::shared_ptr<Config::GrpcMux> {
            EXPECT_NE(primary_async_client, nullptr);
            return authority_A_mux_;
          }));

  // Have a single config_source with two authorities in it.
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_1.com
    api_config_source:
      api_type: AGGREGATED_DELTA_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source1_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source1_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));
}

// Test only a non-default config source defined with two authorities.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, NonDefaultConfigSourceTwoAuthorities) {
  testing::InSequence s;
  // Have a single config_source with two authorities in it.
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_1.com
    - name: authority_2.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source1_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source1_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF",
             true);
}

// Test a non-default and default config source (valid) with the same authority in both.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultAndNonDefaultConfigSources) {
  testing::InSequence s;
  // Have a config-source and default_config_source with authority_2.com in each of them.
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_1.com
    - name: authority_2.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source1_cluster
  default_config_source:
    authorities:
    - name: authority_2.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source1_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: default_config_source_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF",
             true, false, true);
}

// Test a default only config source with repeated authority (invalid).
TEST_F(XdsManagerImplXdstpConfigSourcesTest, NonDefaultConfigSourceRepeatedAuthority) {
  testing::InSequence s;
  // Have a single config_source option with repeated authorities in it.
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_1.com
    - name: authority_1.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source1_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source1_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF");
  const auto res = xds_manager_impl_.initializeAdsConnections(server_.bootstrap_);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(res.message(),
              HasSubstr("xdstp-based config source authority authority_1.com is configured more "
                        "than once in an xdstp-based config source."));
}

// Validate that both the non-default and default config source mux objects are
// started.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultAndNonDefaultMuxesStarted) {
  testing::InSequence s;
  // Have a config-source and default_config_source.
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_1.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source1_cluster
  default_config_source:
    authorities:
    - name: authority_2.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source1_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: default_config_source_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF",
             true, false, true);

  // Validate that start() is invoked on all mux objects.
  EXPECT_CALL(*authority_A_mux_, start());
  EXPECT_CALL(*default_mux_, start());
  xds_manager_impl_.startXdstpAdsMuxes();
}

// Validates that when a single valid config source is defined, a subscription to a resource
// under that authority uses the config source's gRPC mux.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, SubscribeSingleValidConfigSource) {
  testing::InSequence s;
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_A.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_cluster
  static_resources:
    clusters:
    - name: config_source_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
  )EOF",
             true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  const std::string resource_name = "xdstp://authority_A.com/some/resource";
  const std::string type_url = "type.googleapis.com/some.Type";

  absl::StatusOr<xds::core::v3::ResourceName> resource_urn_or_error =
      XdsResourceIdentifier::decodeUrn(resource_name);
  ASSERT_TRUE(resource_urn_or_error.ok());
  xds::core::v3::ResourceName resource_urn = resource_urn_or_error.value();

  NiceMock<MockAdsConfigSubscriptionFactory> config_sub_factory;
  Registry::InjectFactory<ConfigSubscriptionFactory> config_sub_registry(config_sub_factory);
  testing::NiceMock<MockSubscription>* mock_subscription =
      new testing::NiceMock<MockSubscription>();
  ON_CALL(config_sub_factory, create(_))
      .WillByDefault(testing::Invoke(
          [this, &mock_subscription](
              ConfigSubscriptionFactory::SubscriptionData& data) -> Config::SubscriptionPtr {
            EXPECT_EQ(data.ads_grpc_mux_, authority_A_mux_);
            SubscriptionPtr mock_subscription_ptr(mock_subscription);
            return mock_subscription_ptr;
          }));

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, {}, type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_OK(result.status());
  EXPECT_EQ(result.value().get(), mock_subscription);
}

// Validates that when multiple config sources are defined and the first one is valid
// for the resource's authority, that first config source's gRPC mux is used.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, MultipleConfigSourcesUseFirstConfigSource) {
  testing::InSequence s;
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_A.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_A_cluster
  - authorities:
    - name: authority_B.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_B_cluster
  static_resources:
    clusters:
    - name: config_source_A_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source_A_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: config_source_B_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source_B_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF",
             true, true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  // Resource is under authority_A.com.
  const std::string resource_name = "xdstp://authority_A.com/some/resource";
  const std::string type_url = "type.googleapis.com/some.Type";

  absl::StatusOr<xds::core::v3::ResourceName> resource_urn_or_error =
      XdsResourceIdentifier::decodeUrn(resource_name);
  ASSERT_TRUE(resource_urn_or_error.ok());

  NiceMock<MockAdsConfigSubscriptionFactory> config_sub_factory;
  Registry::InjectFactory<ConfigSubscriptionFactory> config_sub_registry(config_sub_factory);
  testing::NiceMock<MockSubscription>* mock_subscription =
      new testing::NiceMock<MockSubscription>();
  ON_CALL(config_sub_factory, create(_))
      .WillByDefault(testing::Invoke(
          [this, &mock_subscription](
              ConfigSubscriptionFactory::SubscriptionData& data) -> Config::SubscriptionPtr {
            // Expect the subscription to use authority_A_mux_.
            EXPECT_EQ(data.ads_grpc_mux_, authority_A_mux_);
            SubscriptionPtr mock_subscription_ptr(mock_subscription);
            return mock_subscription_ptr;
          }));

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, {}, type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_OK(result.status());
  EXPECT_EQ(result.value().get(), mock_subscription);
}

// Validates that when multiple config sources are defined and the first one is NOT valid
// for the resource's authority, but a subsequent one IS, that subsequent config source's
// gRPC mux is used.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, MultipleConfigSourcesUseSecondConfigSource) {
  testing::InSequence s;
  initialize(R"EOF(
  config_sources:
  - authorities: # Config source for authority_A
    - name: authority_A.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_A_cluster
  - authorities: # Config source for authority_B
    - name: authority_B.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_B_cluster
  static_resources:
    clusters:
    - name: config_source_A_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source_A_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: config_source_B_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source_B_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF",
             true, true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  // Resource is under authority_B.com, so authority_A_mux should be skipped.
  const std::string resource_name = "xdstp://authority_B.com/some/resource";
  const std::string type_url = "type.googleapis.com/some.Type";

  absl::StatusOr<xds::core::v3::ResourceName> resource_urn_or_error =
      XdsResourceIdentifier::decodeUrn(resource_name);
  ASSERT_TRUE(resource_urn_or_error.ok());

  NiceMock<MockAdsConfigSubscriptionFactory> config_sub_factory;
  Registry::InjectFactory<ConfigSubscriptionFactory> config_sub_registry(config_sub_factory);
  testing::NiceMock<MockSubscription>* mock_subscription =
      new testing::NiceMock<MockSubscription>();
  ON_CALL(config_sub_factory, create(_))
      .WillByDefault(testing::Invoke(
          [this, &mock_subscription](
              ConfigSubscriptionFactory::SubscriptionData& data) -> Config::SubscriptionPtr {
            // Expect the subscription to use authority_B_mux_.
            EXPECT_EQ(data.ads_grpc_mux_, authority_B_mux_);
            SubscriptionPtr mock_subscription_ptr(mock_subscription);
            return mock_subscription_ptr;
          }));

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, {}, type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_OK(result.status());
  EXPECT_EQ(result.value().get(), mock_subscription);
}

// Validates that when multiple config sources are defined and non are valid for the resource's
// authority, then the non-xDS-TP based subscription is used.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, MultipleConfigSourcesNonMatching) {
  testing::InSequence s;
  initialize(R"EOF(
  config_sources:
  - authorities: # Config source for authority_A
    - name: authority_A.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_A_cluster
  - authorities: # Config source for authority_B
    - name: authority_B.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_B_cluster
  static_resources:
    clusters:
    - name: config_source_A_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source_A_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: config_source_B_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source_B_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF",
             true, true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  // Resource is under authority_C.com, so authority_A_mux and authority_b_mux should be skipped.
  const std::string resource_name = "xdstp://authority_C.com/some/resource";
  const std::string type_url = "type.googleapis.com/some.Type";

  absl::StatusOr<xds::core::v3::ResourceName> resource_urn_or_error =
      XdsResourceIdentifier::decodeUrn(resource_name);
  ASSERT_TRUE(resource_urn_or_error.ok());

  NiceMock<MockAdsConfigSubscriptionFactory> config_sub_factory;
  Registry::InjectFactory<ConfigSubscriptionFactory> config_sub_registry(config_sub_factory);
  testing::NiceMock<MockSubscription>* mock_subscription =
      new testing::NiceMock<MockSubscription>();
  ON_CALL(config_sub_factory, create(_))
      .WillByDefault(testing::Invoke(
          [&mock_subscription](
              ConfigSubscriptionFactory::SubscriptionData& data) -> Config::SubscriptionPtr {
            // Expect the subscription to create the non-grpc_mux version.
            EXPECT_EQ(data.ads_grpc_mux_, nullptr);
            SubscriptionPtr mock_subscription_ptr(mock_subscription);
            return mock_subscription_ptr;
          }));

  envoy::config::core::v3::ConfigSource config_source_proto;
  // For this test, we'll use a basic ADS config source.
  // The key is that subscribeToSingletonResource should call subscriptionFromConfigSource.
  config_source_proto.mutable_ads();
  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, makeOptRef(config_source_proto), type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_OK(result.status());
  EXPECT_EQ(result.value().get(), mock_subscription);
}

// Validates that when config_sources is empty and a valid default_config_source is provided,
// a subscription to a resource matching the default authority uses the default_config_source's mux.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultSourceUsedWhenConfigSourcesIsEmpty) {
  testing::InSequence s;
  initialize(R"EOF(
  default_config_source:
    authorities:
    - name: default_authority.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: default_config_source_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11003 # Different port for clarity
  )EOF",
             false, false, true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  // Resource is under default_authority.com
  const std::string resource_name = "xdstp://default_authority.com/some/resource";
  const std::string type_url = "type.googleapis.com/some.Type";

  absl::StatusOr<xds::core::v3::ResourceName> resource_urn_or_error =
      XdsResourceIdentifier::decodeUrn(resource_name);
  ASSERT_TRUE(resource_urn_or_error.ok());

  NiceMock<MockAdsConfigSubscriptionFactory> config_sub_factory;
  Registry::InjectFactory<ConfigSubscriptionFactory> config_sub_registry(config_sub_factory);
  testing::NiceMock<MockSubscription>* mock_subscription =
      new testing::NiceMock<MockSubscription>();
  ON_CALL(config_sub_factory, create(_))
      .WillByDefault(testing::Invoke(
          [this, &mock_subscription](
              ConfigSubscriptionFactory::SubscriptionData& data) -> Config::SubscriptionPtr {
            // Expect the subscription to use default_mux_
            EXPECT_EQ(data.ads_grpc_mux_, default_mux_);
            SubscriptionPtr mock_subscription_ptr(mock_subscription);
            return mock_subscription_ptr;
          }));

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, {}, type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_OK(result.status());
  EXPECT_EQ(result.value().get(), mock_subscription);
}

// Validates that when config_sources has entries but none match the resource's authority,
// and a valid default_config_source IS provided and matches, the default_config_source's mux is
// used.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, DefaultSourceUsedWhenAllConfigSourcesAreInvalid) {
  testing::InSequence s;
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_A.com # Does not match resource
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_A_cluster
  - authorities:
    - name: authority_B.com # Does not match resource
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_B_cluster
  default_config_source:
    authorities:
    - name: default_authority.com # Matches resource
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source_A_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source_A_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: config_source_B_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source_B_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
    - name: default_config_source_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11003
  )EOF",
             true, true, true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  // Resource is under default_authority.com.
  const std::string resource_name = "xdstp://default_authority.com/some/resource";
  const std::string type_url = "type.googleapis.com/some.Type";

  absl::StatusOr<xds::core::v3::ResourceName> resource_urn_or_error =
      XdsResourceIdentifier::decodeUrn(resource_name);
  ASSERT_TRUE(resource_urn_or_error.ok());

  NiceMock<MockAdsConfigSubscriptionFactory> config_sub_factory;
  Registry::InjectFactory<ConfigSubscriptionFactory> config_sub_registry(config_sub_factory);
  testing::NiceMock<MockSubscription>* mock_subscription =
      new testing::NiceMock<MockSubscription>();
  ON_CALL(config_sub_factory, create(_))
      .WillByDefault(testing::Invoke(
          [this, &mock_subscription](
              ConfigSubscriptionFactory::SubscriptionData& data) -> Config::SubscriptionPtr {
            // Expect the subscription to use default_mux_.
            EXPECT_EQ(data.ads_grpc_mux_, default_mux_);
            SubscriptionPtr mock_subscription_ptr(mock_subscription);
            return mock_subscription_ptr;
          }));

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, {}, type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_OK(result.status());
  EXPECT_EQ(result.value().get(), mock_subscription);
}

// Validates that if config_sources is empty (or all invalid) and default_config_source is also
// not present or invalid for the resource, the subscription fails with NotFoundError.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, SubscriptionFailsIfNoValidSourceIncludingDefault) {
  // Bootstrap with no config_sources and no default_config_source.
  initialize(R"EOF(
  static_resources:
    clusters:
    - name: some_other_cluster # Needed to pass bootstrap validation
      connect_timeout: 0.250s
      type: static
      load_assignment:
        cluster_name: some_other_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11004
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  const std::string resource_name = "xdstp://non_existent_authority.com/some/resource";
  const std::string type_url = "type.googleapis.com/some.Type";

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, {}, type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_THAT(
      result.status().message(),
      HasSubstr(fmt::format("No valid authority was found for the given xDS-TP resource {}.",
                            resource_name)));
}

// Validates that an xdstp resource subscription fails with NotFoundError when there are
// config_sources defined, but none match the authority, and no default_config_source is present.
TEST_F(XdsManagerImplXdstpConfigSourcesTest,
       XdstpResourceNoMatchingAuthorityAndNoDefaultConfigSource) {
  // Bootstrap with a config_source for "authority_A.com" but no default_config_source.
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_A.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source_A_cluster
  static_resources:
    clusters:
    - name: config_source_A_cluster # Needed for the config_source
      connect_timeout: 0.250s
      type: static
      load_assignment:
        cluster_name: config_source_A_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: some_other_cluster # Needed to pass bootstrap validation if no ADS
      connect_timeout: 0.250s
      type: static
      load_assignment:
        cluster_name: some_other_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11004
  )EOF",
             true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  // Request a resource under "authority_X.com", which is not configured.
  const std::string resource_name = "xdstp://authority_X.com/some/resource";
  const std::string type_url = "type.googleapis.com/some.Type";

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, {}, type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_THAT(
      result.status().message(),
      HasSubstr(fmt::format("No valid authority was found for the given xDS-TP resource {}.",
                            resource_name)));
}

// Validates that a non-xdstp resource subscription uses the SubscriptionFactory directly.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, NonXdstpResourceSubscription) {
  // No GrpcMuxFactory needed for this type of subscription if not using ADS for it.
  // Initialize with a basic bootstrap.
  initialize(R"EOF(
  static_resources:
    clusters:
    - name: some_cluster # Needed to pass bootstrap validation
      connect_timeout: 0.250s
      type: STATIC
      load_assignment:
        cluster_name: some_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11005
  )EOF");
  // This call is still needed to initialize the subscription_factory_.
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  const std::string resource_name = "my_legacy_resource"; // Not an xdstp:// URN
  const std::string type_url = "type.googleapis.com/some.legacy.Type";

  envoy::config::core::v3::ConfigSource config_source_proto;
  // For this test, we'll use a basic ADS config source.
  // The key is that subscribeToSingletonResource should call subscriptionFromConfigSource.
  config_source_proto.mutable_ads();

  // The XdsManagerImpl creates its own SubscriptionFactoryImpl.
  // SubscriptionFactoryImpl then uses registered factories (like MockAdsConfigSubscriptionFactory)
  // when subscriptionFromConfigSource is called with a ConfigSource that specifies ADS.
  NiceMock<MockAdsConfigSubscriptionFactory> ads_sub_factory;
  Registry::InjectFactory<ConfigSubscriptionFactory> ads_sub_registry(ads_sub_factory);
  testing::NiceMock<MockSubscription>* mock_subscription =
      new testing::NiceMock<MockSubscription>();

  EXPECT_CALL(ads_sub_factory, create(_))
      .WillOnce(
          Invoke([&](ConfigSubscriptionFactory::SubscriptionData& data) -> Config::SubscriptionPtr {
            EXPECT_EQ(data.config_.config_source_specifier_case(),
                      envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds);
            EXPECT_EQ(data.type_url_, type_url);
            // Check other relevant fields if necessary, e.g., scope, callbacks, resource_decoder,
            // options
            return std::unique_ptr<MockSubscription>(mock_subscription);
          }));

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, makeOptRef(config_source_proto), type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_OK(result.status());
  ASSERT_NE(result.value(), nullptr);
  EXPECT_EQ(result.value().get(), mock_subscription);
}

// Validates that when an xdstp resource is subscribed with a peer ConfigSource
// that has a matching authority, an UnimplementedError is returned.
TEST_F(XdsManagerImplXdstpConfigSourcesTest,
       XdstpResourceWithMatchingPeerConfigSourceAuthorityReturnsUnimplemented) {
  // Initialize with a basic bootstrap. The bootstrap config_sources are not used in this path.
  initialize(R"EOF(
  static_resources:
    clusters:
    - name: some_cluster
      connect_timeout: 0.250s
      type: STATIC
      load_assignment:
        cluster_name: some_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11006
  )EOF");
  EXPECT_OK(xds_manager_impl_.initializeAdsConnections(server_.bootstrap_));

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  const std::string resource_name = "xdstp://peer_authority.com/path/to/resource";
  const std::string type_url = "type.googleapis.com/some.xdstp.Type";

  envoy::config::core::v3::ConfigSource peer_config_source;
  peer_config_source.add_authorities()->set_name("peer_authority.com");
  // Set a basic api_config_source, though it won't be used as it hits the unimplemented path first.
  peer_config_source.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC);
  peer_config_source.mutable_api_config_source()
      ->add_grpc_services()
      ->mutable_envoy_grpc()
      ->set_cluster_name("some_cluster_for_peer_cs");

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, makeOptRef(peer_config_source), type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
  EXPECT_THAT(result.status().message(),
              HasSubstr("Dynamically using non-bootstrap defined xDS-TP config sources is not yet "
                        "supported."));
}

// Validates that when a peer ConfigSource is provided with non-matching authorities
// for an xdstp resource, and bootstrap config_sources also don't match,
// the subscription falls back to the bootstrap default_config_source.
TEST_F(XdsManagerImplXdstpConfigSourcesTest,
       PeerConfigSourceAuthoritiesDontMatchResourceFallsBackToBootstrapDefault) {
  testing::InSequence s;
  initialize(R"EOF(
  config_sources: [] # Empty, or could have irrelevant authorities
  default_config_source:
    authorities:
    - name: target_authority.com # This authority matches the resource
    api_config_source:
      api_type: AGGREGATED_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: default_cs_cluster
  static_resources:
    clusters:
    - name: default_cs_cluster
      connect_timeout: 0.250s
      type: STATIC
      load_assignment:
        cluster_name: default_cs_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address: { address: 127.0.0.1, port_value: 11009 }
    - name: peer_specific_cluster # Cluster for the peer_config_source
      connect_timeout: 0.250s
      type: STATIC
      load_assignment:
        cluster_name: peer_specific_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address: { address: 127.0.0.1, port_value: 11010 }
  )EOF",
             false, false, true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  const std::string resource_name = "xdstp://target_authority.com/path/to/resource";
  const std::string type_url = "type.googleapis.com/some.Type.v0";

  envoy::config::core::v3::ConfigSource peer_config_source;
  // This authority in peer_config_source does NOT match the resource_name's authority.
  peer_config_source.add_authorities()->set_name("some_other_unrelated_authority.com");
  peer_config_source.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC);
  peer_config_source.mutable_api_config_source()
      ->add_grpc_services()
      ->mutable_envoy_grpc()
      ->set_cluster_name("peer_specific_cluster");

  NiceMock<MockAdsConfigSubscriptionFactory> config_sub_factory;
  Registry::InjectFactory<ConfigSubscriptionFactory> config_sub_registry(config_sub_factory);
  testing::NiceMock<MockSubscription>* mock_subscription =
      new testing::NiceMock<MockSubscription>();

  EXPECT_CALL(config_sub_factory, create(_))
      .WillOnce(Invoke([this, mock_subscription](ConfigSubscriptionFactory::SubscriptionData& data)
                           -> Config::SubscriptionPtr {
        // Expect the subscription to use default_mux_ (from bootstrap's default_config_source)
        EXPECT_EQ(data.ads_grpc_mux_, default_mux_);
        // The config used should be the one from the default_config_source in bootstrap.
        EXPECT_EQ(data.config_.authorities(0).name(), "target_authority.com");
        EXPECT_EQ(data.config_.api_config_source().grpc_services(0).envoy_grpc().cluster_name(),
                  "default_cs_cluster");
        return SubscriptionPtr(mock_subscription);
      }));

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, makeOptRef(peer_config_source), type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_OK(result.status());
  ASSERT_NE(result.value(), nullptr);
  EXPECT_EQ(result.value().get(), mock_subscription);
}

// Validates that when a peer ConfigSource is provided with non-matching authorities
// for an xdstp resource, the subscription falls back to bootstrap config sources (default in this
// case).
TEST_F(XdsManagerImplXdstpConfigSourcesTest,
       PeerConfigSourceAuthoritiesDontMatchResourceFallsBackToDefault) {
  testing::InSequence s;
  initialize(R"EOF(
  # config_sources is empty
  default_config_source:
    authorities:
    - name: default_authority.com # This should be used
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: default_config_source_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11007
    - name: peer_cs_cluster # Cluster for the peer_config_source, not expected to be used for mux
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: peer_cs_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11008
  )EOF",
             false, false, true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  const std::string resource_name =
      "xdstp://default_authority.com/path/to/resource"; // Resource authority matches default
  const std::string type_url = "type.googleapis.com/some.xdstp.Type.vN";

  envoy::config::core::v3::ConfigSource peer_config_source;
  peer_config_source.add_authorities()->set_name(
      "authority_X.com"); // This authority does NOT match resource_name
  peer_config_source.mutable_api_config_source()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC);
  peer_config_source.mutable_api_config_source()
      ->add_grpc_services()
      ->mutable_envoy_grpc()
      ->set_cluster_name("peer_cs_cluster");

  NiceMock<MockAdsConfigSubscriptionFactory> config_sub_factory;
  Registry::InjectFactory<ConfigSubscriptionFactory> config_sub_registry(config_sub_factory);
  testing::NiceMock<MockSubscription>* mock_subscription =
      new testing::NiceMock<MockSubscription>();

  EXPECT_CALL(config_sub_factory, create(_))
      .WillOnce(Invoke([this, mock_subscription](ConfigSubscriptionFactory::SubscriptionData& data)
                           -> Config::SubscriptionPtr {
        // Expect the subscription to use default_mux_ from bootstrap
        EXPECT_EQ(data.ads_grpc_mux_, default_mux_);
        // The config used should be the one from the default_config_source in bootstrap,
        // NOT the peer_config_source passed in the call.
        EXPECT_EQ(data.config_.authorities(0).name(), "default_authority.com");
        EXPECT_EQ(data.config_.api_config_source().grpc_services(0).envoy_grpc().cluster_name(),
                  "default_config_source_cluster");
        return SubscriptionPtr(mock_subscription);
      }));

  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, makeOptRef(peer_config_source), type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_OK(result.status());
  ASSERT_NE(result.value(), nullptr);
  EXPECT_EQ(result.value().get(), mock_subscription);
}

// Validates that when a non-xDS-TP resource is passed, then config_source must be passed.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, NonXdstpResourceRequiresConfigSource) {
  testing::InSequence s;
  initialize(R"EOF(
  default_config_source:
    authorities:
    - name: default_authority.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: default_config_source_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11003 # Different port for clarity
  )EOF",
             false, false, true);

  NiceMock<MockSubscriptionCallbacks> callbacks;
  SubscriptionOptions options;
  const std::string resource_name = "non-xdstp-resource";
  const std::string type_url = "type.googleapis.com/some.Type";

  // Pass the non-xdstp resource name without a config.
  auto result = xds_manager_impl_.subscribeToSingletonResource(
      resource_name, {}, type_url, *stats_.rootScope(), callbacks,
      std::make_shared<NiceMock<MockOpaqueResourceDecoder>>(), options);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      result.status().message(),
      HasSubstr(fmt::format("Given subscrption to resource {} must either have an xDS-TP based "
                            "resource or a config must be provided.",
                            resource_name)));
}

// Validate that the pause-resume works on all gRPC-based ADS mux objects.
TEST_F(XdsManagerImplXdstpConfigSourcesTest, PauseResume) {
  testing::InSequence s;
  // Have a config-source and default_config_source with authority_2.com in each of them.
  initialize(R"EOF(
  config_sources:
  - authorities:
    - name: authority_1.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: config_source1_cluster
  default_config_source:
    authorities:
    - name: authority_2.com
    api_config_source:
      api_type: AGGREGATED_GRPC
      set_node_on_first_message_only: true
      grpc_services:
        envoy_grpc:
          cluster_name: default_config_source_cluster
  static_resources:
    clusters:
    - name: config_source1_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: config_source1_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11001
    - name: default_config_source_cluster
      connect_timeout: 0.250s
      type: static
      lb_policy: round_robin
      load_assignment:
        cluster_name: default_config_source_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 11002
  )EOF",
             true, false, true);

  // Validate pause() on a single type.
  {
    const std::string type_url = "type.googleapis.com/some.Type";
    const std::vector<std::string> types{type_url};
    bool authority_a_resumed = false;
    bool default_authority_resumed = false;

    // Validate that pause() on a single type is invoked on the underlying authorities mux objects,
    // and that resume() is invoked when the cleanup object goes out of scope.
    {
      EXPECT_CALL(*authority_A_mux_, pause(types))
          .WillOnce(testing::Invoke(
              [&authority_a_resumed](const std::vector<std::string>) -> ScopedResume {
                return std::make_unique<Cleanup>(
                    [&authority_a_resumed]() { authority_a_resumed = true; });
              }));
      EXPECT_CALL(*default_mux_, pause(types))
          .WillOnce(testing::Invoke(
              [&default_authority_resumed](const std::vector<std::string>) -> ScopedResume {
                return std::make_unique<Cleanup>(
                    [&default_authority_resumed]() { default_authority_resumed = true; });
              }));
      ScopedResume pause_object = xds_manager_impl_.pause(type_url);

      // When the pause object gets out of scope, the resume should be invoked.
      EXPECT_FALSE(authority_a_resumed);
      EXPECT_FALSE(default_authority_resumed);
    }
    // The pause object is out of scope, the authorities should be resumed.
    EXPECT_TRUE(authority_a_resumed);
    EXPECT_TRUE(default_authority_resumed);
  }

  // Validate pause() on multiple types.
  {
    const std::string type_url1 = "type.googleapis.com/some.Type1";
    const std::string type_url2 = "type.googleapis.com/some.Type2";
    const std::vector<std::string> types{type_url1, type_url2};
    bool authority_a_resumed = false;
    bool default_authority_resumed = false;

    // Validate that pause() on multiple types is invoked on the underlying authorities mux objects,
    // and that resume() is invoked when the cleanup object goes out of scope.
    {
      EXPECT_CALL(*authority_A_mux_, pause(types))
          .WillOnce(testing::Invoke(
              [&authority_a_resumed](const std::vector<std::string>) -> ScopedResume {
                return std::make_unique<Cleanup>(
                    [&authority_a_resumed]() { authority_a_resumed = true; });
              }));
      EXPECT_CALL(*default_mux_, pause(types))
          .WillOnce(testing::Invoke(
              [&default_authority_resumed](const std::vector<std::string>) -> ScopedResume {
                return std::make_unique<Cleanup>(
                    [&default_authority_resumed]() { default_authority_resumed = true; });
              }));
      ScopedResume pause_object = xds_manager_impl_.pause(types);

      // When the pause object gets out of scope, the resume should be invoked.
      EXPECT_FALSE(authority_a_resumed);
      EXPECT_FALSE(default_authority_resumed);
    }
    // The pause object is out of scope, the authorities should be resumed.
    EXPECT_TRUE(authority_a_resumed);
    EXPECT_TRUE(default_authority_resumed);
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
