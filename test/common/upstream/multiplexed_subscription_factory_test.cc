#include <memory>

#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"

#include "source/common/upstream/multiplexed_subscription_factory.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/config/custom_config_validators.h"
#include "test/mocks/config/mocks.h"
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
using ::testing::ReturnRef;

namespace Envoy {
namespace Upstream {

class MultiplexedSubscriptionFactoryPeer {
public:
  static size_t optimizedMuxesSize(MultiplexedSubscriptionFactory& factory) {
    return factory.muxes_.size();
  }
};

class MultiplexedSubscriptionFactoryForTesting : public MultiplexedSubscriptionFactory {
public:
  MultiplexedSubscriptionFactoryForTesting(
      const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
      Upstream::ClusterManager& cm, Api::Api& api,
      ProtobufMessage::ValidationVisitor& validation_visitor, const Server::Instance& server,
      Config::XdsResourcesDelegateOptRef xds_resources_delegate, Config::XdsConfigTrackerOptRef xds_config_tracker)
      : MultiplexedSubscriptionFactory(local_info, dispatcher, cm, validation_visitor, api, server,
                                       xds_resources_delegate, xds_config_tracker){};

  Config::GrpcMuxSharedPtr
  testGetOrCreateMux(const envoy::config::core::v3::ApiConfigSource& api_config_source,
                     absl::string_view type_url, Stats::Scope& scope,
                     Config::CustomConfigValidatorsPtr& custom_config_validators) {
    return MultiplexedSubscriptionFactory::getOrCreateMux(api_config_source, type_url, scope,
                                                          custom_config_validators);
  }
};

class MultiplexedSubscriptionFactoryTest
    : public ::testing::TestWithParam<envoy::config::core::v3::ApiConfigSource::ApiType> {
public:
  MultiplexedSubscriptionFactoryTest()
      : type_url_("type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment"),
        resource_decoder_(std::make_shared<NiceMock<Config::MockOpaqueResourceDecoder>>()){};

  NiceMock<Upstream::MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Config::MockSubscriptionCallbacks> callbacks_;
  Stats::MockIsolatedStatsStore stats_store_;
  NiceMock<Api::MockApi> api_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Grpc::MockAsyncClient* async_client_;
  const std::string type_url_;
  Config::RateLimitSettings rate_limit_settings_;
  NiceMock<Server::MockInstance> server_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Config::OpaqueResourceDecoderSharedPtr resource_decoder_;
  NiceMock<Config::XdsResourcesDelegateOptRef> xds_resources_delegate_;
  NiceMock<Config::XdsConfigTrackerOptRef> xds_config_tracker_;
};

using MultiplexedSubscriptionFactoryForGrpcTest = MultiplexedSubscriptionFactoryTest;

INSTANTIATE_TEST_SUITE_P(GrpcApiConfigSource, MultiplexedSubscriptionFactoryForGrpcTest,
                         ::testing::Values(envoy::config::core::v3::ApiConfigSource::GRPC,
                                           envoy::config::core::v3::ApiConfigSource::DELTA_GRPC));

// Verify the same mux instance is returned for same config source.
TEST_P(MultiplexedSubscriptionFactoryForGrpcTest, ShouldReturnSameMuxForSameConfigSource) {
  envoy::config::core::v3::ConfigSource config1;
  auto config_source = config1.mutable_api_config_source();
  config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("primary_xds_cluster");
  config_source->set_api_type(GetParam());
  auto factory = MultiplexedSubscriptionFactoryForTesting(
      local_info_, dispatcher_, cm_, api_, validation_visitor_, server_, xds_resources_delegate_, xds_config_tracker_);

  EXPECT_CALL(dispatcher_, createTimer_(_));
  Config::CustomConfigValidatorsPtr config_validators =
      std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto first_mux = factory.testGetOrCreateMux(config1.api_config_source(), type_url_, stats_store_,
                                              config_validators);
  config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto second_mux = factory.testGetOrCreateMux(config1.api_config_source(), type_url_, stats_store_,
                                               config_validators);
  EXPECT_EQ(first_mux.get(), second_mux.get());
  EXPECT_EQ(1, MultiplexedSubscriptionFactoryPeer::optimizedMuxesSize(factory));
}

// Verify the same mux instance is returned when the same management servers are used.
TEST_P(MultiplexedSubscriptionFactoryForGrpcTest, ShouldReturnSameMuxForSameGrpcService) {
  envoy::config::core::v3::ConfigSource config1;
  auto config_source = config1.mutable_api_config_source();
  config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("primary_xds_cluster");
  config_source->set_api_type(GetParam());
  envoy::config::core::v3::ConfigSource config2;
  config_source = config2.mutable_api_config_source();
  config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("primary_xds_cluster");
  config_source->set_api_type(GetParam());
  config_source = config2.mutable_api_config_source();
  config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "fallback_xds_cluster");
  auto factory = MultiplexedSubscriptionFactoryForTesting(
      local_info_, dispatcher_, cm_, api_, validation_visitor_, server_, xds_resources_delegate_, xds_config_tracker_);

  EXPECT_CALL(dispatcher_, createTimer_(_));
  Config::CustomConfigValidatorsPtr config_validators =
      std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto first_mux = factory.testGetOrCreateMux(config1.api_config_source(), type_url_, stats_store_,
                                              config_validators);
  config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto second_mux = factory.testGetOrCreateMux(config1.api_config_source(), type_url_, stats_store_,
                                               config_validators);
  EXPECT_EQ(first_mux.get(), second_mux.get());
  EXPECT_EQ(1, MultiplexedSubscriptionFactoryPeer::optimizedMuxesSize(factory));
}

// Verify that a new mux instance is created if a different config_source is used.
TEST_P(MultiplexedSubscriptionFactoryForGrpcTest, ShouldReturnDiffMuxesForDiffXdsServers) {
  auto factory = MultiplexedSubscriptionFactoryForTesting(
      local_info_, dispatcher_, cm_, api_, validation_visitor_, server_, xds_resources_delegate_, xds_config_tracker_);

  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(2);
  envoy::config::core::v3::ConfigSource first_config;
  auto config_source = first_config.mutable_api_config_source();
  config_source->set_api_type(GetParam());
  config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("first_cluster");
  Config::CustomConfigValidatorsPtr config_validators =
      std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto first_mux = factory.testGetOrCreateMux(first_config.api_config_source(), type_url_,
                                              stats_store_, config_validators);
  envoy::config::core::v3::ConfigSource second_config;
  config_source = second_config.mutable_api_config_source();
  config_source->set_api_type(GetParam());
  config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("second_cluster");
  config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto second_mux = factory.testGetOrCreateMux(second_config.api_config_source(), type_url_,
                                               stats_store_, config_validators);
  EXPECT_NE(first_mux.get(), second_mux.get());
  EXPECT_EQ(2, MultiplexedSubscriptionFactoryPeer::optimizedMuxesSize(factory));
}

// Verify that a new mux instance is created if a different xds method is used.
TEST_P(MultiplexedSubscriptionFactoryForGrpcTest, ShouldReturnDiffMuxesForDiffXdsTypes) {
  envoy::config::core::v3::ConfigSource config;
  auto config_source = config.mutable_api_config_source();
  config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("xds_cluster");
  config_source->set_api_type(GetParam());
  auto factory = MultiplexedSubscriptionFactoryForTesting(
      local_info_, dispatcher_, cm_, api_, validation_visitor_, server_, xds_resources_delegate_, xds_config_tracker_);
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(3);
  Config::CustomConfigValidatorsPtr config_validators =
      std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto first_mux = factory.testGetOrCreateMux(config.api_config_source(), type_url_, stats_store_,
                                              config_validators);
  config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto second_mux = factory.testGetOrCreateMux(
      config.api_config_source(), "type.googleapis.com/envoy.config.cluster.v3.Cluster",
      stats_store_, config_validators);
  EXPECT_NE(first_mux.get(), second_mux.get());
  config_validators = std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto third_mux = factory.testGetOrCreateMux(
      config.api_config_source(), "type.googleapis.com/envoy.config.listener.v3.Listener",
      stats_store_, config_validators);
  EXPECT_NE(first_mux.get(), third_mux.get());
  EXPECT_NE(second_mux.get(), third_mux.get());
  EXPECT_EQ(3, MultiplexedSubscriptionFactoryPeer::optimizedMuxesSize(factory));
}

// Verify that muxes managed by MultiplexedSubscriptionFactory are used when ApiConfigSource::GRPC
// api type is used.
TEST_P(MultiplexedSubscriptionFactoryForGrpcTest,
       ShouldUseGetOrCreateMuxWhenApiConfigSourceIsUsed) {
  auto factory = MultiplexedSubscriptionFactoryForTesting(
      local_info_, dispatcher_, cm_, api_, validation_visitor_, server_, xds_resources_delegate_, xds_config_tracker_);
  envoy::config::core::v3::ConfigSource config;
  auto config_source = config.mutable_api_config_source();
  config_source->set_api_type(GetParam());
  config_source->set_transport_api_version(envoy::config::core::v3::V3);
  config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("first_cluster");
  Upstream::ClusterManager::ClusterSet primary_clusters;
  primary_clusters.insert("first_cluster");
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
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
  Config::CustomConfigValidatorsPtr config_validators =
      std::make_unique<NiceMock<Config::MockCustomConfigValidators>>();
  auto expected_mux = factory.testGetOrCreateMux(config.api_config_source(), type_url_,
                                                 stats_store_, config_validators);
  EXPECT_EQ(expected_mux.get(),
            (dynamic_cast<Config::GrpcSubscriptionImpl&>(*subscription).grpcMux()).get());
}

using MultiplexedSubscriptionFactoryForNonGrpcTest = MultiplexedSubscriptionFactoryTest;

INSTANTIATE_TEST_SUITE_P(NonGrpcApiConfigSource, MultiplexedSubscriptionFactoryForNonGrpcTest,
                         ::testing::Values(envoy::config::core::v3::ApiConfigSource::REST));

TEST_P(MultiplexedSubscriptionFactoryForNonGrpcTest,
       ShouldUseBaseGetOrCreateMuxWhenNonGrpcConfigSourceIsUsed) {
  auto factory = MultiplexedSubscriptionFactoryForTesting(
      local_info_, dispatcher_, cm_, api_, validation_visitor_, server_, xds_resources_delegate_, xds_config_tracker_);
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
  api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
  api_config_source->mutable_refresh_delay()->set_seconds(1);
  api_config_source->mutable_request_timeout()->set_seconds(5);
  api_config_source->add_cluster_names("xds_cluster");
  Upstream::ClusterManager::ClusterSet primary_clusters;
  primary_clusters.insert("xds_cluster");
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_CALL(dispatcher_, createTimer_(_));
  factory.subscriptionFromConfigSource(config, Config::TypeUrl::get().ClusterLoadAssignment,
                                       stats_store_, callbacks_, resource_decoder_, {});
  EXPECT_EQ(0, MultiplexedSubscriptionFactoryPeer::optimizedMuxesSize(factory));
}

} // namespace Upstream
} // namespace Envoy
