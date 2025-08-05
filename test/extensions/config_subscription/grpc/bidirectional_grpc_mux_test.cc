#include "source/extensions/config_subscription/grpc/bidirectional_grpc_mux.h"
#include "source/extensions/config_subscription/grpc/listener_status_provider.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

class BidirectionalGrpcMuxTest : public testing::Test {
public:
  BidirectionalGrpcMuxTest() {
    EXPECT_CALL(local_info_, node()).WillRepeatedly(testing::ReturnRef(node_));
  }

  void setup() {
    // Create a mock base GrpcMux
    auto base_mux = std::make_shared<NiceMock<Config::MockGrpcMux>>();
    
    // Create bidirectional mux using composition
    bidirectional_mux_ = std::make_unique<BidirectionalGrpcMuxImpl>(base_mux);
  }

protected:
  envoy::config::core::v3::Node node_;
  envoy::config::core::v3::ApiConfigSource ads_config_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Grpc::MockAsyncClientFactory> async_client_factory_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Stats::ScopeSharedPtr stats_scope_{stats_store_.createScope("test.")};
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  
  Grpc::MockAsyncClient* async_client_{};
  std::unique_ptr<BidirectionalGrpcMuxImpl> bidirectional_mux_;
};

TEST_F(BidirectionalGrpcMuxTest, RegisterClientResourceProvider) {
  setup();
  
  // Create a mock resource provider
  auto provider = std::make_unique<Server::ListenerStatusProvider>();
  std::string type_url = provider->getTypeUrl();
  
  // Register the provider
  bidirectional_mux_->registerClientResourceProvider(type_url, std::move(provider));
  
  // The provider should now be registered
  // (In a real test, we'd verify this by calling getProvider, but that's not public)
}

TEST_F(BidirectionalGrpcMuxTest, HandleReverseDiscoveryRequest) {
  setup();
  
  // Register a listener status provider
  auto provider = std::make_unique<Server::ListenerStatusProvider>();
  
  // Add some test data
  provider->onListenerAdded("test_listener");
  provider->onListenerReady("test_listener", "0.0.0.0:80");
  
  std::string type_url = provider->getTypeUrl();
  bidirectional_mux_->registerClientResourceProvider(type_url, std::move(provider));
  
  // For now, we just test that the provider registration doesn't crash
  // In a full implementation, we would test the reverse xDS functionality
  // when the bidirectional stream callbacks are properly implemented
}

// Test for listener status provider specifically
class ListenerStatusProviderTest : public testing::Test {
protected:
  Server::ListenerStatusProvider provider_;
};

TEST_F(ListenerStatusProviderTest, BasicFunctionality) {
  // Test basic listener lifecycle
  EXPECT_EQ(provider_.getTypeUrl(), "type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus");
  EXPECT_EQ(provider_.getVersionInfo(), "1");
  
  // Add a listener
  provider_.onListenerAdded("test_listener");
  EXPECT_EQ(provider_.getVersionInfo(), "2"); // Version should increment
  
  // Make it ready
  provider_.onListenerReady("test_listener", "0.0.0.0:80");
  EXPECT_EQ(provider_.getVersionInfo(), "3");
  
  // Get resources
  auto resources = provider_.getResources({});
  EXPECT_EQ(resources.size(), 1);
  
  // Verify the resource contains proper data
  envoy::admin::v3::ListenerReadinessStatus status;
  EXPECT_TRUE(resources[0].UnpackTo(&status));
  EXPECT_EQ(status.listener_name(), "test_listener");
  EXPECT_TRUE(status.ready());
  EXPECT_EQ(status.bound_address(), "0.0.0.0:80");
  EXPECT_EQ(status.state(), envoy::admin::v3::ListenerReadinessStatus::READY);
}

TEST_F(ListenerStatusProviderTest, ListenerFailure) {
  // Test listener failure scenario
  provider_.onListenerAdded("failing_listener");
  provider_.onListenerFailed("failing_listener", "Port already in use");
  
  auto resources = provider_.getResources({"failing_listener"});
  EXPECT_EQ(resources.size(), 1);
  
  envoy::admin::v3::ListenerReadinessStatus status;
  EXPECT_TRUE(resources[0].UnpackTo(&status));
  EXPECT_EQ(status.listener_name(), "failing_listener");
  EXPECT_FALSE(status.ready());
  EXPECT_EQ(status.error_message(), "Port already in use");
  EXPECT_EQ(status.state(), envoy::admin::v3::ListenerReadinessStatus::FAILED);
}

TEST_F(ListenerStatusProviderTest, SelectiveResourceRetrieval) {
  // Add multiple listeners
  provider_.onListenerAdded("listener_1");
  provider_.onListenerReady("listener_1", "0.0.0.0:80");
  
  provider_.onListenerAdded("listener_2");
  provider_.onListenerReady("listener_2", "0.0.0.0:443");
  
  provider_.onListenerAdded("listener_3");
  provider_.onListenerFailed("listener_3", "Permission denied");
  
  // Get all resources
  auto all_resources = provider_.getResources({});
  EXPECT_EQ(all_resources.size(), 3);
  
  // Get specific resources
  auto specific_resources = provider_.getResources({"listener_1", "listener_3"});
  EXPECT_EQ(specific_resources.size(), 2);
  
  // Verify the resources are correct
  for (const auto& resource : specific_resources) {
    envoy::admin::v3::ListenerReadinessStatus status;
    EXPECT_TRUE(resource.UnpackTo(&status));
    EXPECT_TRUE(status.listener_name() == "listener_1" || status.listener_name() == "listener_3");
  }
}

} // namespace
} // namespace Config
} // namespace Envoy 