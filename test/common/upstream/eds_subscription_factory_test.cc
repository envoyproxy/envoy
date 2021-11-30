#include <memory>

#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"

#include "source/common/upstream/eds_subscription_factory.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/config/custom_config_validators.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace Envoy {
namespace Upstream {

class EdsSubscriptionFactoryForTesting : public EdsSubscriptionFactory {
public:
  EdsSubscriptionFactoryForTesting(const LocalInfo::LocalInfo& local_info,
                                   Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm,
                                   Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor, 
                         const Server::Instance& server)
      : EdsSubscriptionFactory(local_info, dispatcher, cm, api, validation_visitor, server){};

  Config::GrpcMuxSharedPtr
  testGetOrCreateMux(Grpc::RawAsyncClientPtr async_client,
                     const Protobuf::MethodDescriptor& service_method,
                     Random::RandomGenerator& random,
                     const envoy::config::core::v3::ApiConfigSource& config_source,
                     Stats::Scope& scope, const Config::RateLimitSettings& rate_limit_settings, 
                     Config::CustomConfigValidatorsPtr& custom_config_validators) {

    return EdsSubscriptionFactory::getOrCreateMux(std::move(async_client), service_method, random,
                                                  config_source, scope, rate_limit_settings, custom_config_validators);
  }
};

class EdsSubscriptionFactoryTest : public ::testing::Test {
public:
  EdsSubscriptionFactoryTest()
      : method_descriptor_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.api.v3.EndpointDiscoveryService.StreamEndpoints")) {}

  NiceMock<Upstream::MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Config::MockSubscriptionCallbacks> callbacks_;
  Stats::MockIsolatedStatsStore stats_store_;
  NiceMock<Api::MockApi> api_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Grpc::MockAsyncClient* async_client_;
  const Protobuf::MethodDescriptor& method_descriptor_;
  Config::RateLimitSettings rate_limit_settings_;
  NiceMock<Server::MockInstance> server_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Config::MockOpaqueResourceDecoder> resource_decoder_; 
};

// Verify the same mux instance is returned for same config source.
TEST_F(EdsSubscriptionFactoryTest, ShouldReturnSameMuxForSameConfigSource) {
  envoy::config::core::v3::ConfigSource config1;
  config1.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "primary_eds_cluster");
  auto factory = EdsSubscriptionFactoryForTesting(local_info_, dispatcher_, cm_, api_, validation_visitor_, server_);

  EXPECT_CALL(dispatcher_, createTimer_(_));
  Config::CustomConfigValidatorsPtr config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto first_mux = factory.testGetOrCreateMux(
      std::make_unique<Grpc::MockAsyncClient>(), method_descriptor_, random_,
      config1.api_config_source(), stats_store_, rate_limit_settings_, config_validators);
  config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>(); 
  auto second_mux = factory.testGetOrCreateMux(
      std::make_unique<Grpc::MockAsyncClient>(), method_descriptor_, random_,
      config1.api_config_source(), stats_store_, rate_limit_settings_, config_validators);
  EXPECT_EQ(first_mux.get(), second_mux.get());
}

// Verify the same mux instance is returned when the same management servers are used.
TEST_F(EdsSubscriptionFactoryTest, ShouldReturnSameMuxForSameGrpcService) {
  envoy::config::core::v3::ConfigSource config1;
  config1.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "primary_eds_cluster");
  envoy::config::core::v3::ConfigSource config2;
  config2.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "primary_eds_cluster");
  config2.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "fallback_eds_cluster");
  auto factory = EdsSubscriptionFactoryForTesting(local_info_, dispatcher_, cm_, api_, validation_visitor_, server_);

  EXPECT_CALL(dispatcher_, createTimer_(_));
  Config::CustomConfigValidatorsPtr config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto first_mux = factory.testGetOrCreateMux(
      std::make_unique<Grpc::MockAsyncClient>(), method_descriptor_, random_,
      config1.api_config_source(), stats_store_, rate_limit_settings_, config_validators);
  config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>(); 
  auto second_mux = factory.testGetOrCreateMux(
      std::make_unique<Grpc::MockAsyncClient>(), method_descriptor_, random_,
      config2.api_config_source(), stats_store_, rate_limit_settings_, config_validators);
  EXPECT_EQ(first_mux.get(), second_mux.get());
}

// Verify that a new mux instance is created if a different config_source is used.
TEST_F(EdsSubscriptionFactoryTest, ShouldReturnDifferentMuxes) {
  auto factory = EdsSubscriptionFactoryForTesting(local_info_, dispatcher_, cm_, api_, validation_visitor_, server_);

  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(2);
  envoy::config::core::v3::ConfigSource first_config;
  first_config.mutable_api_config_source()
      ->add_grpc_services()
      ->mutable_envoy_grpc()
      ->set_cluster_name("first_cluster");
  Config::CustomConfigValidatorsPtr config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto first_mux = factory.testGetOrCreateMux(
      std::make_unique<Grpc::MockAsyncClient>(), method_descriptor_, random_,
      first_config.api_config_source(), stats_store_, rate_limit_settings_, config_validators);
  envoy::config::core::v3::ConfigSource second_config;
  second_config.mutable_api_config_source()
      ->add_grpc_services()
      ->mutable_envoy_grpc()
      ->set_cluster_name("second_cluster");
  config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>(); 
  auto second_mux = factory.testGetOrCreateMux(
      std::make_unique<Grpc::MockAsyncClient>(), method_descriptor_, random_,
      second_config.api_config_source(), stats_store_, rate_limit_settings_, config_validators);
  EXPECT_NE(first_mux.get(), second_mux.get());
}

// Verify that muxes managed by EdsSubscriptionFactory are used when ApiConfigSource::GRPC api type
// is used.
TEST_F(EdsSubscriptionFactoryTest, ShouldUseGetOrCreateMuxWhenApiConfigSourceIsUsed) {
  auto factory = EdsSubscriptionFactoryForTesting(local_info_, dispatcher_, cm_, api_, validation_visitor_, server_);

  envoy::config::core::v3::ConfigSource config;
  config.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  config.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "first_cluster");

  NiceMock<Grpc::MockAsyncClientFactory>* async_client_factory{
      new NiceMock<Grpc::MockAsyncClientFactory>()};
  EXPECT_CALL(*async_client_factory, createUncachedRawAsyncClient()).WillOnce(Invoke([] {
    return std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
  }));

  NiceMock<Grpc::MockAsyncClientManager> async_client_manager;
  EXPECT_CALL(async_client_manager, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke(
          [async_client_factory](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
            return Grpc::AsyncClientFactoryPtr(async_client_factory);
          }));
  EXPECT_CALL(cm_, grpcAsyncClientManager()).WillOnce(ReturnRef(async_client_manager));
  EXPECT_CALL(dispatcher_, createTimer_(_));

  auto subscription =
      factory.subscriptionFromConfigSource(config, Config::TypeUrl::get().ClusterLoadAssignment,
                                           stats_store_, callbacks_, resource_decoder_, {});

  Config::CustomConfigValidatorsPtr config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto expected_mux = factory.testGetOrCreateMux(
      std::make_unique<Grpc::MockAsyncClient>(), method_descriptor_, random_,
      config.api_config_source(), stats_store_, rate_limit_settings_, config_validators);

  EXPECT_EQ(expected_mux.get(),
            (dynamic_cast<Config::GrpcSubscriptionImpl&>(*subscription).grpcMux()).get());
}

// Verify that super-class subscriptionFromConfigSource() is called when a non ApiConfigSource::GRPC
// api type is used
TEST_F(EdsSubscriptionFactoryTest, ShouldCallConfigSubscriptionFactory) {
  auto factory = EdsSubscriptionFactoryForTesting(local_info_, dispatcher_, cm_, api_, validation_visitor_, server_);
  NiceMock<Envoy::Config::MockSubscriptionFactory> subscription_factory;
  envoy::config::core::v3::ConfigSource config;
  config.mutable_ads();
  EXPECT_CALL(cm_, subscriptionFactory).WillOnce(ReturnRef(subscription_factory));
  EXPECT_CALL(subscription_factory, subscriptionFromConfigSource);
  factory.subscriptionFromConfigSource(config, Config::TypeUrl::get().ClusterLoadAssignment,
                                       stats_store_, callbacks_, resource_decoder_, {});
}

} // namespace Upstream
} // namespace Envoy