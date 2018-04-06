#include "envoy/api/v2/eds.pb.h"
#include "envoy/common/exception.h"

#include "common/config/subscription_factory.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Invoke;
using ::testing::Return;
using ::testing::_;

namespace Envoy {
namespace Config {

class SubscriptionFactoryTest : public ::testing::Test {
public:
  SubscriptionFactoryTest() : http_request_(&cm_.async_client_) {
    legacy_subscription_.reset(new MockSubscription<envoy::api::v2::ClusterLoadAssignment>());
  }

  std::unique_ptr<Subscription<envoy::api::v2::ClusterLoadAssignment>>
  subscriptionFromConfigSource(const envoy::api::v2::core::ConfigSource& config) {
    return SubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::ClusterLoadAssignment>(
        config, node_, dispatcher_, cm_, random_, stats_store_,
        [this]() -> Subscription<envoy::api::v2::ClusterLoadAssignment>* {
          return legacy_subscription_.release();
        },
        "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints",
        "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints");
  }

  envoy::api::v2::core::Node node_;
  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  Runtime::MockRandomGenerator random_;
  MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
  std::unique_ptr<MockSubscription<envoy::api::v2::ClusterLoadAssignment>> legacy_subscription_;
  Http::MockAsyncClientRequest http_request_;
  Stats::MockIsolatedStatsStore stats_store_;
};

class SubscriptionFactoryTestApiConfigSource
    : public SubscriptionFactoryTest,
      public testing::WithParamInterface<::envoy::api::v2::core::ApiConfigSource_ApiType> {};

TEST_F(SubscriptionFactoryTest, NoConfigSpecifier) {
  envoy::api::v2::core::ConfigSource config;
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config), EnvoyException,
      "Missing config source specifier in envoy::api::v2::core::ConfigSource");
}

TEST_F(SubscriptionFactoryTest, RestClusterEmpty) {
  envoy::api::v2::core::ConfigSource config;
  Upstream::ClusterManager::ClusterInfoMap cluster_map;

  config.mutable_api_config_source()->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config), EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a singleton cluster name specified");
}

TEST_F(SubscriptionFactoryTest, GrpcClusterEmpty) {
  envoy::api::v2::core::ConfigSource config;
  Upstream::ClusterManager::ClusterInfoMap cluster_map;

  config.mutable_api_config_source()->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config), EnvoyException,
      "envoy::api::v2::core::ConfigSource::GRPC must have a single gRPC service specified");
}

TEST_F(SubscriptionFactoryTest, RestClusterSingleton) {
  envoy::api::v2::core::ConfigSource config;
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  NiceMock<Upstream::MockCluster> cluster;

  config.mutable_api_config_source()->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);
  config.mutable_api_config_source()->mutable_refresh_delay()->set_seconds(1);
  config.mutable_api_config_source()->add_cluster_names("static_cluster");
  cluster_map.emplace("static_cluster", cluster);

  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(false));
  EXPECT_CALL(*cluster.info_, type()).WillOnce(Return(envoy::api::v2::Cluster::STATIC));
  subscriptionFromConfigSource(config);
}

TEST_F(SubscriptionFactoryTest, GrpcClusterSingleton) {
  envoy::api::v2::core::ConfigSource config;
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  NiceMock<Upstream::MockCluster> cluster;

  config.mutable_api_config_source()->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
  config.mutable_api_config_source()->mutable_refresh_delay()->set_seconds(1);
  config.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "static_cluster");
  cluster_map.emplace("static_cluster", cluster);

  envoy::api::v2::core::GrpcService expected_grpc_service;
  expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("static_cluster");

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cm_, grpcAsyncClientManager()).WillOnce(ReturnRef(cm_.async_client_manager_));
  EXPECT_CALL(cm_.async_client_manager_,
              factoryForGrpcService(ProtoEq(expected_grpc_service), _, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        auto async_client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
        EXPECT_CALL(*async_client_factory, create()).WillOnce(Invoke([] {
          return std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
        }));
        return async_client_factory;
      }));
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(false));
  EXPECT_CALL(*cluster.info_, type()).WillOnce(Return(envoy::api::v2::Cluster::STATIC));
  EXPECT_CALL(dispatcher_, createTimer_(_));

  subscriptionFromConfigSource(config);
}

TEST_F(SubscriptionFactoryTest, RestClusterMultiton) {
  envoy::api::v2::core::ConfigSource config;
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  NiceMock<Upstream::MockCluster> cluster;

  config.mutable_api_config_source()->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);

  config.mutable_api_config_source()->add_cluster_names("static_cluster_foo");
  cluster_map.emplace("static_cluster_foo", cluster);

  config.mutable_api_config_source()->add_cluster_names("static_cluster_bar");
  cluster_map.emplace("static_cluster_bar", cluster);

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map));
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillRepeatedly(Return(false));
  EXPECT_CALL(*cluster.info_, type()).WillRepeatedly(Return(envoy::api::v2::Cluster::STATIC));
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config), EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a singleton cluster name specified");
}

TEST_F(SubscriptionFactoryTest, GrpcClusterMultiton) {
  envoy::api::v2::core::ConfigSource config;
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  NiceMock<Upstream::MockCluster> cluster;

  config.mutable_api_config_source()->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);

  config.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "static_cluster_foo");
  cluster_map.emplace("static_cluster_foo", cluster);
  config.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "static_cluster_bar");
  cluster_map.emplace("static_cluster_bar", cluster);

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map));
  EXPECT_CALL(cm_, grpcAsyncClientManager()).WillRepeatedly(ReturnRef(cm_.async_client_manager_));
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillRepeatedly(Return(false));
  EXPECT_CALL(*cluster.info_, type()).WillRepeatedly(Return(envoy::api::v2::Cluster::STATIC));

  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config), EnvoyException,
      "envoy::api::v2::core::ConfigSource::GRPC must have a single gRPC service specified");
}

TEST_F(SubscriptionFactoryTest, FilesystemSubscription) {
  envoy::api::v2::core::ConfigSource config;
  std::string test_path = TestEnvironment::temporaryDirectory();
  config.set_path(test_path);
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(test_path, _, _));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  subscriptionFromConfigSource(config)->start({"foo"}, callbacks_);
}

TEST_F(SubscriptionFactoryTest, FilesystemSubscriptionNonExistentFile) {
  envoy::api::v2::core::ConfigSource config;
  config.set_path("/blahblah");
  EXPECT_THROW_WITH_MESSAGE(subscriptionFromConfigSource(config)->start({"foo"}, callbacks_),
                            EnvoyException,
                            "envoy::api::v2::Path must refer to an existing path in the system: "
                            "'/blahblah' does not exist")
}

TEST_F(SubscriptionFactoryTest, LegacySubscription) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::REST_LEGACY);
  api_config_source->add_cluster_names("static_cluster");
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*legacy_subscription_, start(_, _));
  subscriptionFromConfigSource(config)->start({"static_cluster"}, callbacks_);
}

TEST_F(SubscriptionFactoryTest, HttpSubscription) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);
  api_config_source->add_cluster_names("static_cluster");
  api_config_source->mutable_refresh_delay()->set_seconds(1);
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(cm_, httpAsyncClientForCluster("static_cluster"));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([this](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                              const absl::optional<std::chrono::milliseconds>& timeout) {
        UNREFERENCED_PARAMETER(callbacks);
        UNREFERENCED_PARAMETER(timeout);
        EXPECT_EQ("POST", std::string(request->headers().Method()->value().c_str()));
        EXPECT_EQ("static_cluster", std::string(request->headers().Host()->value().c_str()));
        EXPECT_EQ("/v2/discovery:endpoints",
                  std::string(request->headers().Path()->value().c_str()));
        return &http_request_;
      }));
  EXPECT_CALL(http_request_, cancel());
  subscriptionFromConfigSource(config)->start({"static_cluster"}, callbacks_);
}

// Confirm error when no refresh delay is set (not checked by schema).
TEST_F(SubscriptionFactoryTest, HttpSubscriptionNoRefreshDelay) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);
  api_config_source->add_cluster_names("static_cluster");
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config)->start({"static_cluster"}, callbacks_), EnvoyException,
      "refresh_delay is required for REST API configuration sources");
}

TEST_F(SubscriptionFactoryTest, GrpcSubscription) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
  api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("static_cluster");
  envoy::api::v2::core::GrpcService expected_grpc_service;
  expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("static_cluster");
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  NiceMock<Upstream::MockCluster> cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cm_, grpcAsyncClientManager()).WillOnce(ReturnRef(cm_.async_client_manager_));
  EXPECT_CALL(cm_.async_client_manager_,
              factoryForGrpcService(ProtoEq(expected_grpc_service), _, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        auto async_client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
        EXPECT_CALL(*async_client_factory, create()).WillOnce(Invoke([] {
          return std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
        }));
        return async_client_factory;
      }));
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  subscriptionFromConfigSource(config)->start({"static_cluster"}, callbacks_);
}

INSTANTIATE_TEST_CASE_P(SubscriptionFactoryTestApiConfigSource,
                        SubscriptionFactoryTestApiConfigSource,
                        ::testing::Values(envoy::api::v2::core::ApiConfigSource::REST_LEGACY,
                                          envoy::api::v2::core::ApiConfigSource::REST,
                                          envoy::api::v2::core::ApiConfigSource::GRPC));

TEST_P(SubscriptionFactoryTestApiConfigSource, NonExistentCluster) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(GetParam());
  if (api_config_source->api_type() == envoy::api::v2::core::ApiConfigSource::GRPC) {
    api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
        "static_cluster");
  } else {
    api_config_source->add_cluster_names("static_cluster");
  }
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config)->start({"static_cluster"}, callbacks_), EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined "
      "non-EDS cluster: 'static_cluster' does not exist, was added via api, or is an EDS cluster");
}

TEST_P(SubscriptionFactoryTestApiConfigSource, DynamicCluster) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(GetParam());
  if (api_config_source->api_type() == envoy::api::v2::core::ApiConfigSource::GRPC) {
    api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
        "static_cluster");
  } else {
    api_config_source->add_cluster_names("static_cluster");
  }
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config)->start({"static_cluster"}, callbacks_), EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined "
      "non-EDS cluster: 'static_cluster' does not exist, was added via api, or is an EDS cluster");
}

TEST_P(SubscriptionFactoryTestApiConfigSource, EDSClusterBackingEDSCluster) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(GetParam());
  if (api_config_source->api_type() == envoy::api::v2::core::ApiConfigSource::GRPC) {
    api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
        "static_cluster");
  } else {
    api_config_source->add_cluster_names("static_cluster");
  }
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type()).WillOnce(Return(envoy::api::v2::Cluster::EDS));
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config)->start({"static_cluster"}, callbacks_), EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined "
      "non-EDS cluster: 'static_cluster' does not exist, was added via api, or is an EDS cluster");
}

} // namespace Config
} // namespace Envoy
