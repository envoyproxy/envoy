#include <memory>

#include "envoy/api/v2/eds.pb.h"
#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"

#include "common/config/subscription_factory_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace Envoy {
namespace Config {
namespace {

class SubscriptionFactoryTest : public testing::Test {
public:
  SubscriptionFactoryTest()
      : http_request_(&cm_.async_client_), api_(Api::createApiForTest(stats_store_)) {}

  std::unique_ptr<Subscription>
  subscriptionFromConfigSource(const envoy::api::v2::core::ConfigSource& config) {
    return SubscriptionFactoryImpl(local_info_, dispatcher_, cm_, random_, validation_visitor_,
                                   *api_)
        .subscriptionFromConfigSource(config, Config::TypeUrl::get().ClusterLoadAssignment,
                                      stats_store_, callbacks_);
  }

  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  Runtime::MockRandomGenerator random_;
  MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
  Http::MockAsyncClientRequest http_request_;
  Stats::MockIsolatedStatsStore stats_store_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
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
  EXPECT_THROW_WITH_REGEX(subscriptionFromConfigSource(config), EnvoyException,
                          "API configs must have either a gRPC service or a cluster name defined:");
}

TEST_F(SubscriptionFactoryTest, GrpcClusterEmpty) {
  envoy::api::v2::core::ConfigSource config;
  Upstream::ClusterManager::ClusterInfoMap cluster_map;

  config.mutable_api_config_source()->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_THROW_WITH_REGEX(subscriptionFromConfigSource(config), EnvoyException,
                          "API configs must have either a gRPC service or a cluster name defined:");
}

TEST_F(SubscriptionFactoryTest, RestClusterSingleton) {
  envoy::api::v2::core::ConfigSource config;
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;

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
  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;

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
          return std::make_unique<Grpc::MockAsyncClient>();
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
  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;

  config.mutable_api_config_source()->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);

  config.mutable_api_config_source()->add_cluster_names("static_cluster_foo");
  cluster_map.emplace("static_cluster_foo", cluster);

  config.mutable_api_config_source()->add_cluster_names("static_cluster_bar");
  cluster_map.emplace("static_cluster_bar", cluster);

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map));
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillRepeatedly(Return(false));
  EXPECT_CALL(*cluster.info_, type()).WillRepeatedly(Return(envoy::api::v2::Cluster::STATIC));
  EXPECT_THROW_WITH_REGEX(
      subscriptionFromConfigSource(config), EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a singleton cluster name specified:");
}

TEST_F(SubscriptionFactoryTest, GrpcClusterMultiton) {
  envoy::api::v2::core::ConfigSource config;
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;

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

  EXPECT_THROW_WITH_REGEX(subscriptionFromConfigSource(config), EnvoyException,
                          "envoy::api::v2::core::ConfigSource::.DELTA_.GRPC must have a "
                          "single gRPC service specified:");
}

TEST_F(SubscriptionFactoryTest, FilesystemSubscription) {
  envoy::api::v2::core::ConfigSource config;
  std::string test_path = TestEnvironment::temporaryDirectory();
  config.set_path(test_path);
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(test_path, _, _));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
  subscriptionFromConfigSource(config)->start({"foo"});
}

TEST_F(SubscriptionFactoryTest, FilesystemSubscriptionNonExistentFile) {
  envoy::api::v2::core::ConfigSource config;
  config.set_path("/blahblah");
  EXPECT_THROW_WITH_MESSAGE(subscriptionFromConfigSource(config)->start({"foo"}), EnvoyException,
                            "envoy::api::v2::Path must refer to an existing path in the system: "
                            "'/blahblah' does not exist")
}

TEST_F(SubscriptionFactoryTest, LegacySubscription) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::UNSUPPORTED_REST_LEGACY);
  api_config_source->add_cluster_names("static_cluster");
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockClusterMockPrioritySet cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_THROW_WITH_REGEX(subscriptionFromConfigSource(config)->start({"static_cluster"}),
                          EnvoyException, "REST_LEGACY no longer a supported ApiConfigSource.*");
}

TEST_F(SubscriptionFactoryTest, HttpSubscriptionCustomRequestTimeout) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);
  api_config_source->add_cluster_names("static_cluster");
  api_config_source->mutable_refresh_delay()->set_seconds(1);
  api_config_source->mutable_request_timeout()->set_seconds(5);
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockClusterMockPrioritySet cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(2);
  EXPECT_CALL(cm_, httpAsyncClientForCluster("static_cluster"));
  EXPECT_CALL(
      cm_.async_client_,
      send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5000))));
  subscriptionFromConfigSource(config)->start({"static_cluster"});
}

TEST_F(SubscriptionFactoryTest, HttpSubscription) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);
  api_config_source->add_cluster_names("static_cluster");
  api_config_source->mutable_refresh_delay()->set_seconds(1);
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockClusterMockPrioritySet cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(2);
  EXPECT_CALL(cm_, httpAsyncClientForCluster("static_cluster"));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([this](Http::MessagePtr& request, Http::AsyncClient::Callbacks&,
                              const Http::AsyncClient::RequestOptions&) {
        EXPECT_EQ("POST", std::string(request->headers().Method()->value().getStringView()));
        EXPECT_EQ("static_cluster",
                  std::string(request->headers().Host()->value().getStringView()));
        EXPECT_EQ("/v2/discovery:endpoints",
                  std::string(request->headers().Path()->value().getStringView()));
        return &http_request_;
      }));
  EXPECT_CALL(http_request_, cancel());
  subscriptionFromConfigSource(config)->start({"static_cluster"});
}

// Confirm error when no refresh delay is set (not checked by schema).
TEST_F(SubscriptionFactoryTest, HttpSubscriptionNoRefreshDelay) {
  envoy::api::v2::core::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::REST);
  api_config_source->add_cluster_names("static_cluster");
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockClusterMockPrioritySet cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_THROW_WITH_MESSAGE(subscriptionFromConfigSource(config)->start({"static_cluster"}),
                            EnvoyException,
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
  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
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
  EXPECT_CALL(random_, random());
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(2);
  // onConfigUpdateFailed() should not be called for gRPC stream connection failure
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _)).Times(0);
  subscriptionFromConfigSource(config)->start({"static_cluster"});
}

INSTANTIATE_TEST_SUITE_P(SubscriptionFactoryTestApiConfigSource,
                         SubscriptionFactoryTestApiConfigSource,
                         ::testing::Values(envoy::api::v2::core::ApiConfigSource::REST,
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
      subscriptionFromConfigSource(config)->start({"static_cluster"}), EnvoyException,
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
  Upstream::MockClusterMockPrioritySet cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info());
  EXPECT_CALL(*cluster.info_, addedViaApi()).WillOnce(Return(true));
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config)->start({"static_cluster"}), EnvoyException,
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
  Upstream::MockClusterMockPrioritySet cluster;
  cluster_map.emplace("static_cluster", cluster);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type()).WillOnce(Return(envoy::api::v2::Cluster::EDS));
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config)->start({"static_cluster"}), EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined "
      "non-EDS cluster: 'static_cluster' does not exist, was added via api, or is an EDS cluster");
}

} // namespace
} // namespace Config
} // namespace Envoy
