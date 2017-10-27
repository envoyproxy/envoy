#include "envoy/common/exception.h"

#include "common/config/subscription_factory.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "api/eds.pb.h"
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
  subscriptionFromConfigSource(const envoy::api::v2::ConfigSource& config) {
    return SubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::ClusterLoadAssignment>(
        config, node_, dispatcher_, cm_, random_, stats_store_,
        [this]() -> Subscription<envoy::api::v2::ClusterLoadAssignment>* {
          return legacy_subscription_.release();
        },
        "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints",
        "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints");
  }

  envoy::api::v2::Node node_;
  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  Runtime::MockRandomGenerator random_;
  MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment> callbacks_;
  std::unique_ptr<MockSubscription<envoy::api::v2::ClusterLoadAssignment>> legacy_subscription_;
  Http::MockAsyncClientRequest http_request_;
  Stats::MockIsolatedStatsStore stats_store_;
};

TEST_F(SubscriptionFactoryTest, NoConfigSpecifier) {
  envoy::api::v2::ConfigSource config;
  EXPECT_THROW_WITH_MESSAGE(subscriptionFromConfigSource(config), EnvoyException,
                            "Missing config source specifier in envoy::api::v2::ConfigSource");
}

TEST_F(SubscriptionFactoryTest, WrongClusterNameLength) {
  envoy::api::v2::ConfigSource config;
  config.mutable_api_config_source()->set_api_type(envoy::api::v2::ApiConfigSource::REST);
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config), EnvoyException,
      "envoy::api::v2::ConfigSource must have a singleton cluster name specified");
  config.mutable_api_config_source()->add_cluster_name("foo");
  config.mutable_api_config_source()->add_cluster_name("bar");
  EXPECT_THROW_WITH_MESSAGE(
      subscriptionFromConfigSource(config), EnvoyException,
      "envoy::api::v2::ConfigSource must have a singleton cluster name specified");
}

TEST_F(SubscriptionFactoryTest, FilesystemSubscription) {
  envoy::api::v2::ConfigSource config;
  config.set_path("/blahblah");
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch("/blahblah", _, _));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_));
  subscriptionFromConfigSource(config)->start({"foo"}, callbacks_);
}

TEST_F(SubscriptionFactoryTest, LegacySubscription) {
  envoy::api::v2::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::ApiConfigSource::REST_LEGACY);
  api_config_source->add_cluster_name("eds_cluster");
  EXPECT_CALL(*legacy_subscription_, start(_, _));
  subscriptionFromConfigSource(config)->start({"foo"}, callbacks_);
}

TEST_F(SubscriptionFactoryTest, HttpSubscription) {
  envoy::api::v2::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::ApiConfigSource::REST);
  api_config_source->add_cluster_name("eds_cluster");
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(cm_, httpAsyncClientForCluster("eds_cluster"));
  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([this](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                              const Optional<std::chrono::milliseconds>& timeout) {
        UNREFERENCED_PARAMETER(callbacks);
        UNREFERENCED_PARAMETER(timeout);
        EXPECT_EQ("POST", std::string(request->headers().Method()->value().c_str()));
        EXPECT_EQ("eds_cluster", std::string(request->headers().Host()->value().c_str()));
        EXPECT_EQ("/v2/discovery:endpoints",
                  std::string(request->headers().Path()->value().c_str()));
        return &http_request_;
      }));
  EXPECT_CALL(http_request_, cancel());
  subscriptionFromConfigSource(config)->start({"foo"}, callbacks_);
}

TEST_F(SubscriptionFactoryTest, GrpcSubscription) {
  envoy::api::v2::ConfigSource config;
  auto* api_config_source = config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::ApiConfigSource::GRPC);
  api_config_source->add_cluster_name("eds_cluster");
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(cm_, httpAsyncClientForCluster("eds_cluster"));
  NiceMock<Http::MockAsyncClientStream> stream;
  EXPECT_CALL(cm_.async_client_, start(_, _, false)).WillOnce(Return(&stream));
  Http::TestHeaderMapImpl headers{
      {":method", "POST"},
      {":path", "/envoy.api.v2.EndpointDiscoveryService/StreamEndpoints"},
      {":authority", "eds_cluster"},
      {"content-type", "application/grpc"},
      {"te", "trailers"}};
  EXPECT_CALL(stream, sendHeaders(HeaderMapEqualRef(&headers), _));
  EXPECT_CALL(cm_.async_client_.dispatcher_, deferredDelete_(_));
  subscriptionFromConfigSource(config)->start({"foo"}, callbacks_);
}

} // namespace Config
} // namespace Envoy
