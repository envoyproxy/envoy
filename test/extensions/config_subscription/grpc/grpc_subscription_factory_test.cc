#include "source/common/config/utility.h"
#include "source/extensions/config_subscription/grpc/grpc_subscription_factory.h"
#include "source/extensions/config_subscription/grpc/grpc_subscription_impl.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Config {
namespace {

class GrpcSubscriptionFactoryTest : public testing::Test {
protected:
  GrpcSubscriptionFactoryTest() {
    ON_CALL(cm_, grpcAsyncClientManager()).WillByDefault(ReturnRef(async_client_manager_));
  }

  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Api::MockApi> api_;
  NiceMock<Server::MockInstance> server_;
  envoy::config::core::v3::ConfigSource config_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Config::MockSubscriptionCallbacks> callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_;
  SubscriptionOptions options_;
  NiceMock<Grpc::MockAsyncClientManager> async_client_manager_;
};

// Validates that the creation of a GrpcSubscription is successful.
TEST_F(GrpcSubscriptionFactoryTest, CreateSucceeded) {
  GrpcConfigSubscriptionFactory factory;
  // Setup config with GRPC
  auto* api_config_source = config_.mutable_api_config_source();
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
  api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");

  SubscriptionStats stats = Utility::generateStats(*stats_.rootScope());

  ConfigSubscriptionFactory::SubscriptionData data{
      local_info_,
      dispatcher_,
      cm_,
      validation_visitor_,
      api_,
      server_,
      {},
      {},
      config_,
      "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment",
      *stats_.rootScope(),
      callbacks_,
      resource_decoder_,
      options_,
      {},
      stats,
      nullptr};

  auto async_client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
  auto* factory_ptr = async_client_factory.get();
  EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Return(testing::ByMove(std::move(async_client_factory))));

  auto mock_async_client = std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
  EXPECT_CALL(*factory_ptr, createUncachedRawAsyncClient())
      .WillOnce(Return(testing::ByMove(std::move(mock_async_client))));

  SubscriptionPtr subscription;
  EXPECT_NO_THROW(subscription = factory.create(data));
}

// Validates that the creation of a GrpcSubscription with a LRS factory is successful.
TEST_F(GrpcSubscriptionFactoryTest, CreateWithLrsSucceeded) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.enable_lrs_server_self_ads", "true"},
                              {"envoy.reloadable_features.unified_mux", "false"}});

  GrpcConfigSubscriptionFactory factory;
  // Setup config with GRPC
  auto* api_config_source = config_.mutable_api_config_source();
  api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
  api_config_source->add_grpc_services()->mutable_envoy_grpc()->set_cluster_name("foo");

  SubscriptionStats stats = Utility::generateStats(*stats_.rootScope());

  ConfigSubscriptionFactory::SubscriptionData data{
      local_info_,
      dispatcher_,
      cm_,
      validation_visitor_,
      api_,
      server_,
      {},
      {},
      config_,
      "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment",
      *stats_.rootScope(),
      callbacks_,
      resource_decoder_,
      options_,
      {},
      stats,
      nullptr};

  auto async_client_factory = std::make_unique<Grpc::MockAsyncClientFactory>();
  auto* factory_ptr = async_client_factory.get();
  EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Return(testing::ByMove(std::move(async_client_factory))));

  auto mock_async_client = std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
  EXPECT_CALL(*factory_ptr, createUncachedRawAsyncClient())
      .WillOnce(Return(testing::ByMove(std::move(mock_async_client))));

  SubscriptionPtr subscription = factory.create(data);

  // Exercise the LRS lambda by calling maybeCreateLoadStatsReporter on the mux.
  auto* grpc_sub = dynamic_cast<GrpcSubscriptionImpl*>(subscription.get());
  ASSERT_NE(nullptr, grpc_sub);
  auto mux = grpc_sub->grpcMux();
  auto* grpc_mux = dynamic_cast<GrpcMuxImpl*>(mux.get());
  if (grpc_mux) {
    grpc_mux->maybeCreateLoadStatsReporter();
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
