#include <memory>

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/config_source.pb.validate.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
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
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;

namespace Envoy {
namespace Config {
namespace {

class SubscriptionFactoryTest : public testing::Test {
public:
  SubscriptionFactoryTest()
      : http_request_(&cm_.async_client_), api_(Api::createApiForTest(stats_store_)),
        subscription_factory_(local_info_, dispatcher_, cm_, random_, validation_visitor_, *api_,
                              runtime_) {}

  std::unique_ptr<Subscription>
  subscriptionFromConfigSource(const envoy::config::core::v3::ConfigSource& config) {
    return subscription_factory_.subscriptionFromConfigSource(
        config, Config::TypeUrl::get().ClusterLoadAssignment, stats_store_, callbacks_,
        resource_decoder_);
  }

  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  Runtime::MockRandomGenerator random_;
  MockSubscriptionCallbacks callbacks_;
  MockOpaqueResourceDecoder resource_decoder_;
  Http::MockAsyncClientRequest http_request_;
  Stats::MockIsolatedStatsStore stats_store_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
  NiceMock<Runtime::MockLoader> runtime_;
  SubscriptionFactoryImpl subscription_factory_;
};

class SubscriptionFactoryTestApiConfigSource
    : public SubscriptionFactoryTest,
      public testing::WithParamInterface<envoy::config::core::v3::ApiConfigSource::ApiType> {};

TEST_F(SubscriptionFactoryTest, NoConfigSpecifier) {
  envoy::config::core::v3::ConfigSource config;
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config), EnvoyException,
      "Missing config source specifier in envoy::api::v2::core::ConfigSource");
}

TEST_F(SubscriptionFactoryTest, RestClusterEmpty) {
  envoy::config::core::v3::ConfigSource config;
  Upstream::ClusterManager::ClusterSet primary_clusters;

  config.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::REST);

  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_THROW_WITH_REGEX(subscriptionFromConfigSource(config), EnvoyException,
                          "API configs must have either a gRPC service or a cluster name defined:");
}

TEST_F(SubscriptionFactoryTest, GrpcClusterEmpty) {
  envoy::config::core::v3::ConfigSource config;
  Upstream::ClusterManager::ClusterSet primary_clusters;

  config.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);

  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_THROW_WITH_REGEX(subscriptionFromConfigSource(config), EnvoyException,
                          "API configs must have either a gRPC service or a cluster name defined:");
}

TEST_F(SubscriptionFactoryTest, RestClusterSingleton) {
  envoy::config::core::v3::ConfigSource config;
  Upstream::ClusterManager::ClusterSet primary_clusters;

  config.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
  config.mutable_api_config_source()->mutable_refresh_delay()->set_seconds(1);
  config.mutable_api_config_source()->add_cluster_names("static_cluster");
  primary_clusters.insert("static_cluster");

  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  subscriptionFromConfigSource(config);
}

TEST_F(SubscriptionFactoryTest, GrpcClusterSingleton) {
  envoy::config::core::v3::ConfigSource config;
  Upstream::ClusterManager::ClusterSet primary_clusters;

  config.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  config.mutable_api_config_source()->mutable_refresh_delay()->set_seconds(1);
  config.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "static_cluster");
  primary_clusters.insert("static_cluster");

  envoy::config::core::v3::GrpcService expected_grpc_service;
  expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("static_cluster");

  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_CALL(cm_, grpcAsyncClientManager()).WillOnce(ReturnRef(cm_.async_client_manager_));
  EXPECT_CALL(cm_.async_client_manager_,
              factoryForGrpcService(ProtoEq(expected_grpc_service), _, _))
      .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
        auto async_client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
        EXPECT_CALL(*async_client_factory, create()).WillOnce(Invoke([] {
          return std::make_unique<Grpc::MockAsyncClient>();
        }));
        return async_client_factory;
      }));
  EXPECT_CALL(dispatcher_, createTimer_(_));

  subscriptionFromConfigSource(config);
}

TEST_F(SubscriptionFactoryTest, RestClusterMultiton) {
  envoy::config::core::v3::ConfigSource config;
  Upstream::ClusterManager::ClusterSet primary_clusters;

  config.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::REST);

  config.mutable_api_config_source()->add_cluster_names("static_cluster_foo");
  primary_clusters.insert("static_cluster_foo");

  config.mutable_api_config_source()->add_cluster_names("static_cluster_bar");
  primary_clusters.insert("static_cluster_bar");

  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_THROW_WITH_REGEX(subscriptionFromConfigSource(config), EnvoyException,
                          fmt::format("{} must have a singleton cluster name specified:",
                                      config.mutable_api_config_source()->GetTypeName()));
}

TEST_F(SubscriptionFactoryTest, GrpcClusterMultiton) {
  envoy::config::core::v3::ConfigSource config;
  Upstream::ClusterManager::ClusterSet primary_clusters;

  config.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);

  config.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "static_cluster_foo");
  primary_clusters.insert("static_cluster_foo");
  config.mutable_api_config_source()->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
      "static_cluster_bar");
  primary_clusters.insert("static_cluster_bar");

  EXPECT_CALL(cm_, grpcAsyncClientManager()).WillRepeatedly(ReturnRef(cm_.async_client_manager_));
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));

  EXPECT_THROW_WITH_REGEX(subscriptionFromConfigSource(config), EnvoyException,
                          fmt::format("{}::.DELTA_.GRPC must have a "
                                      "single gRPC service specified:",
                                      config.mutable_api_config_source()->GetTypeName()));
}

TEST_F(SubscriptionFactoryTest, FilesystemSubscription) {
  envoy::config::core::v3::ConfigSource config;
  std::string test_path = TestEnvironment::temporaryDirectory();
  config.set_path(test_path);
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(test_path, _, _));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
  subscriptionFromConfigSource(config)->start({"foo"});
}

TEST_F(SubscriptionFactoryTest, FilesystemSubscriptionNonExistentFile) {
  envoy::config::core::v3::ConfigSource config;
  config.set_path("/blahblah");
  EXPECT_THROW_WITH_MESSAGE(subscriptionFromConfigSource(config)->start({"foo"}), EnvoyException,
                            "envoy::api::v2::Path must refer to an existing path in the system: "
                            "'/blahblah' does not exist")
}

TEST_F(SubscriptionFactoryTest, LegacySubscription) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(
      envoy::config::core::v3::ApiConfigSource::hidden_envoy_deprecated_UNSUPPORTED_REST_LEGACY);
  api_config_source->add_cluster_names("static_cluster");
  Upstream::ClusterManager::ClusterSet primary_clusters;
  primary_clusters.insert("static_cluster");
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_THROW_WITH_REGEX(subscriptionFromConfigSource(config)->start({"static_cluster"}),
                          EnvoyException, "REST_LEGACY no longer a supported ApiConfigSource.*");
}

TEST_F(SubscriptionFactoryTest, HttpSubscriptionCustomRequestTimeout) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
  api_config_source->add_cluster_names("static_cluster");
  api_config_source->mutable_refresh_delay()->set_seconds(1);
  api_config_source->mutable_request_timeout()->set_seconds(5);
  Upstream::ClusterManager::ClusterSet primary_clusters;
  primary_clusters.insert("static_cluster");
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(2);
  EXPECT_CALL(cm_, httpAsyncClientForCluster("static_cluster"));
  EXPECT_CALL(
      cm_.async_client_,
      send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(5000))));
  subscriptionFromConfigSource(config)->start({"static_cluster"});
}

TEST_F(SubscriptionFactoryTest, HttpSubscription) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
  api_config_source->add_cluster_names("static_cluster");
  api_config_source->mutable_refresh_delay()->set_seconds(1);
  Upstream::ClusterManager::ClusterSet primary_clusters;
  primary_clusters.insert("static_cluster");
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(2);
  EXPECT_CALL(cm_, httpAsyncClientForCluster("static_cluster"));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([this](Http::RequestMessagePtr& request, Http::AsyncClient::Callbacks&,
                              const Http::AsyncClient::RequestOptions&) {
        EXPECT_EQ("POST", request->headers().getMethodValue());
        EXPECT_EQ("static_cluster", request->headers().getHostValue());
        EXPECT_EQ("/v2/discovery:endpoints", request->headers().getPathValue());
        return &http_request_;
      }));
  EXPECT_CALL(http_request_, cancel());
  subscriptionFromConfigSource(config)->start({"static_cluster"});
}

// Confirm error when no refresh delay is set (not checked by schema).
TEST_F(SubscriptionFactoryTest, HttpSubscriptionNoRefreshDelay) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::REST);
  api_config_source->add_cluster_names("static_cluster");
  Upstream::ClusterManager::ClusterSet primary_clusters;
  primary_clusters.insert("static_cluster");
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_THROW_WITH_MESSAGE(subscriptionFromConfigSource(config)->start({"static_cluster"}),
                            EnvoyException,
                            "refresh_delay is required for REST API configuration sources");
}

TEST_F(SubscriptionFactoryTest, GrpcSubscription) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("static_cluster");
  envoy::config::core::v3::GrpcService expected_grpc_service;
  expected_grpc_service.mutable_envoy_grpc()->set_cluster_name("static_cluster");
  Upstream::ClusterManager::ClusterSet primary_clusters;
  primary_clusters.insert("static_cluster");
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_CALL(cm_, grpcAsyncClientManager()).WillOnce(ReturnRef(cm_.async_client_manager_));
  EXPECT_CALL(cm_.async_client_manager_,
              factoryForGrpcService(ProtoEq(expected_grpc_service), _, _))
      .WillOnce(Invoke([](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
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

TEST_F(SubscriptionFactoryTest, LogWarningOnDeprecatedApi) {
  envoy::config::core::v3::ConfigSource config;

  config.mutable_api_config_source()->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  config.mutable_api_config_source()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V2);
  NiceMock<Runtime::MockSnapshot> snapshot;
  EXPECT_CALL(runtime_, snapshot()).WillRepeatedly(ReturnRef(snapshot));
  EXPECT_CALL(snapshot, runtimeFeatureEnabled(_)).WillOnce(Return(true));
  EXPECT_CALL(snapshot, countDeprecatedFeatureUse());

  Upstream::ClusterManager::ClusterSet primary_clusters;
  primary_clusters.insert("static_cluster");
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));

  EXPECT_LOG_CONTAINS(
      "warn", "xDS of version v2 has been deprecated", try {
        subscription_factory_.subscriptionFromConfigSource(
            config, Config::TypeUrl::get().ClusterLoadAssignment, stats_store_, callbacks_,
            resource_decoder_);
      } catch (EnvoyException&){/* expected, we pass an empty configuration  */});
}

INSTANTIATE_TEST_SUITE_P(SubscriptionFactoryTestApiConfigSource,
                         SubscriptionFactoryTestApiConfigSource,
                         ::testing::Values(envoy::config::core::v3::ApiConfigSource::REST,
                                           envoy::config::core::v3::ApiConfigSource::GRPC));

TEST_P(SubscriptionFactoryTestApiConfigSource, NonExistentCluster) {
  envoy::config::core::v3::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(GetParam());
  if (api_config_source->api_type() == envoy::config::core::v3::ApiConfigSource::GRPC) {
    api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name(
        "static_cluster");
  } else {
    api_config_source->add_cluster_names("static_cluster");
  }
  Upstream::ClusterManager::ClusterSet primary_clusters;
  EXPECT_CALL(cm_, primaryClusters()).WillOnce(ReturnRef(primary_clusters));
  EXPECT_THROW_WITH_MESSAGE(subscriptionFromConfigSource(config)->start({"static_cluster"}),
                            EnvoyException,
                            fmt::format("{} must have a statically defined "
                                        "non-EDS cluster: 'static_cluster' does not exist, was "
                                        "added via api, or is an EDS cluster",
                                        api_config_source->GetTypeName()));
}

} // namespace
} // namespace Config
} // namespace Envoy
