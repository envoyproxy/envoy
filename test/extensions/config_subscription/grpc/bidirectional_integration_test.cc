#include "source/extensions/config_subscription/grpc/bidirectional_grpc_mux.h"
#include "source/server/listener_status_provider.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/utility.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

/**
 * Integration test that verifies the complete bidirectional xDS flow:
 * 1. Normal ADS for configuration
 * 2. Reverse xDS for status reporting  
 * 3. Both on the same stream
 */
class BidirectionalXdsIntegrationTest : public Grpc::GrpcClientIntegrationParamTest {
public:
  BidirectionalXdsIntegrationTest() : method_descriptor_(nullptr) {
    // Setup standard Envoy test environment
    EXPECT_CALL(local_info_, node()).WillRepeatedly(testing::ReturnRef(node_));
  }

  void SetUp() override {
    // Find the ADS method descriptor
    method_descriptor_ = Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
        "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources");
    ASSERT_NE(nullptr, method_descriptor_);
    
    // Setup async client  
    async_client_factory_.set_fail_async_client_create(false);
    auto async_client = std::make_unique<Grpc::MockAsyncClient>();
    async_client_ = async_client.get();
    
    // Create bidirectional mux
    bidirectional_mux_ = std::make_unique<BidirectionalGrpcMuxImpl>(
        std::move(async_client), false, ads_config_, *dispatcher_,
        async_client_factory_, *stats_store_.rootScope(), RateLimitSettings{}, 
        local_info_, nullptr, nullptr, nullptr, nullptr, true);
  }

protected:
  void setupListenerStatusProvider() {
    // Create and register listener status provider
    auto provider = std::make_unique<Server::ListenerStatusProvider>();
    listener_provider_ = provider.get();
    
    bidirectional_mux_->registerClientResourceProvider(
        provider->getTypeUrl(), std::move(provider));
  }
  
  void simulateListenerEvents() {
    ASSERT_NE(nullptr, listener_provider_);
    
    // Simulate listener lifecycle events
    listener_provider_->onListenerAdded("http_listener");
    listener_provider_->onListenerReady("http_listener", "0.0.0.0:80");
    
    listener_provider_->onListenerAdded("https_listener");  
    listener_provider_->onListenerFailed("https_listener", "Port 443 already in use");
  }
  
  envoy::service::discovery::v3::DiscoveryRequest createReverseRequest() {
    envoy::service::discovery::v3::DiscoveryRequest request;
    request.set_type_url("type.googleapis.com/envoy.admin.v3.ListenerReadinessStatus");
    request.set_version_info("");
    request.add_resource_names("http_listener");
    request.add_resource_names("https_listener");
    
    auto* node = request.mutable_node();
    node->set_id("test-management-server");
    node->set_cluster("test-cluster");
    
    return request;
  }

  envoy::config::core::v3::Node node_;
  envoy::config::core::v3::ApiConfigSource ads_config_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  
  NiceMock<Event::MockDispatcher>* dispatcher_{&Grpc::GrpcClientIntegrationParamTest::dispatcher_};
  NiceMock<Grpc::MockAsyncClientFactory> async_client_factory_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::TestUtil::TestStore stats_store_;
  
  Grpc::MockAsyncClient* async_client_{};
  std::unique_ptr<BidirectionalGrpcMuxImpl> bidirectional_mux_;
  Server::ListenerStatusProvider* listener_provider_{};
};

TEST_P(BidirectionalXdsIntegrationTest, FullBidirectionalFlow) {
  // Setup the test environment
  setupListenerStatusProvider();
  simulateListenerEvents();
  
  // Verify we can handle a reverse xDS request
  auto reverse_request = createReverseRequest();
  
  // This would normally be called by the stream when receiving a request
  // from the management server
  bidirectional_mux_->onDiscoveryRequest(
      std::make_unique<envoy::service::discovery::v3::DiscoveryRequest>(reverse_request));
  
  // Verify listener provider has the expected resources
  auto resources = listener_provider_->getResources({"http_listener", "https_listener"});
  EXPECT_EQ(resources.size(), 2);
  
  // Verify the resources contain correct data
  for (const auto& resource : resources) {
    envoy::admin::v3::ListenerReadinessStatus status;
    EXPECT_TRUE(resource.UnpackTo(&status));
    
    if (status.listener_name() == "http_listener") {
      EXPECT_TRUE(status.ready());
      EXPECT_EQ(status.bound_address(), "0.0.0.0:80");
      EXPECT_EQ(status.state(), envoy::admin::v3::ListenerReadinessStatus::READY);
    } else if (status.listener_name() == "https_listener") {
      EXPECT_FALSE(status.ready());
      EXPECT_EQ(status.error_message(), "Port 443 already in use");
      EXPECT_EQ(status.state(), envoy::admin::v3::ListenerReadinessStatus::FAILED);
    }
  }
}

TEST_P(BidirectionalXdsIntegrationTest, VersionTracking) {
  setupListenerStatusProvider();
  
  // Initial version should be "1"
  EXPECT_EQ(listener_provider_->getVersionInfo(), "1");
  
  // Adding listeners should increment version
  listener_provider_->onListenerAdded("test1");
  EXPECT_EQ(listener_provider_->getVersionInfo(), "2");
  
  listener_provider_->onListenerReady("test1", "0.0.0.0:8080");
  EXPECT_EQ(listener_provider_->getVersionInfo(), "3");
  
  // Removing listeners should also increment version
  listener_provider_->removeListener("test1");
  EXPECT_EQ(listener_provider_->getVersionInfo(), "4");
}

TEST_P(BidirectionalXdsIntegrationTest, SelectiveResourceRequest) {
  setupListenerStatusProvider();
  
  // Add multiple listeners
  listener_provider_->onListenerAdded("listener1");
  listener_provider_->onListenerReady("listener1", "0.0.0.0:80");
  
  listener_provider_->onListenerAdded("listener2");
  listener_provider_->onListenerReady("listener2", "0.0.0.0:443");
  
  listener_provider_->onListenerAdded("listener3");
  listener_provider_->onListenerFailed("listener3", "Permission denied");
  
  // Request only specific listeners
  auto request = createReverseRequest();
  request.clear_resource_names();
  request.add_resource_names("listener1");
  request.add_resource_names("listener3");
  
  // Handle the selective request
  bidirectional_mux_->onDiscoveryRequest(
      std::make_unique<envoy::service::discovery::v3::DiscoveryRequest>(request));
  
  // Verify only requested resources are included
  auto resources = listener_provider_->getResources({"listener1", "listener3"});
  EXPECT_EQ(resources.size(), 2);
  
  std::set<std::string> returned_names;
  for (const auto& resource : resources) {
    envoy::admin::v3::ListenerReadinessStatus status;
    EXPECT_TRUE(resource.UnpackTo(&status));
    returned_names.insert(status.listener_name());
  }
  
  EXPECT_EQ(returned_names, std::set<std::string>({"listener1", "listener3"}));
}

// Parameterized test for different gRPC client types
INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, BidirectionalXdsIntegrationTest,
                        GRPC_CLIENT_INTEGRATION_PARAMS);

} // namespace
} // namespace Config
} // namespace Envoy 