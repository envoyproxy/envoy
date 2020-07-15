#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/service/health/v3/hds.pb.h"
#include "envoy/type/v3/http.pb.h"

#include "common/singleton/manager_impl.h"
#include "common/upstream/health_discovery_service.h"

#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

// Friend class of HdsDelegate, making it easier to access private fields
class HdsDelegateFriend {
public:
  // Allows access to private function processMessage
  void processPrivateMessage(
      HdsDelegate& hd,
      std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>&& message) {
    hd.processMessage(std::move(message));
  };
  HdsDelegateStats getStats(HdsDelegate& hd) { return hd.stats_; };
};

class HdsTest : public testing::Test {
protected:
  HdsTest()
      : retry_timer_(new Event::MockTimer()), server_response_timer_(new Event::MockTimer()),
        async_client_(new Grpc::MockAsyncClient()), api_(Api::createApiForTest(stats_store_)),
        ssl_context_manager_(api_->timeSource()) {
    node_.set_id("hds-node");
  }

  // Creates an HdsDelegate
  void createHdsDelegate() {
    InSequence s;
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      retry_timer_cb_ = timer_cb;
      return retry_timer_;
    }));
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .Times(AtLeast(1))
        .WillOnce(Invoke([this](Event::TimerCb timer_cb) {
          server_response_timer_cb_ = timer_cb;
          return server_response_timer_;
        }));
    hds_delegate_ = std::make_unique<HdsDelegate>(
        stats_store_, Grpc::RawAsyncClientPtr(async_client_),
        envoy::config::core::v3::ApiVersion::AUTO, dispatcher_, runtime_, stats_store_,
        ssl_context_manager_, random_, test_factory_, log_manager_, cm_, local_info_, admin_,
        singleton_manager_, tls_, validation_visitor_, *api_);
  }

  // Creates a HealthCheckSpecifier message that contains one endpoint and one
  // healthcheck
  envoy::service::health::v3::HealthCheckSpecifier* createSimpleMessage() {
    envoy::service::health::v3::HealthCheckSpecifier* msg =
        new envoy::service::health::v3::HealthCheckSpecifier;
    msg->mutable_interval()->set_seconds(1);

    auto* health_check = msg->add_cluster_health_checks();
    health_check->set_cluster_name("anna");
    health_check->add_health_checks()->mutable_timeout()->set_seconds(1);
    health_check->mutable_health_checks(0)->mutable_interval()->set_seconds(1);
    health_check->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(2);
    health_check->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(2);
    health_check->mutable_health_checks(0)->mutable_grpc_health_check();
    health_check->mutable_health_checks(0)->mutable_http_health_check()->set_codec_client_type(
        envoy::type::v3::HTTP1);
    health_check->mutable_health_checks(0)->mutable_http_health_check()->set_path("/healthcheck");

    auto* socket_address = health_check->add_locality_endpoints()
                               ->add_endpoints()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("127.0.0.0");
    socket_address->set_port_value(1234);

    return msg;
  }

  Event::SimulatedTimeSystem time_system_;
  envoy::config::core::v3::Node node_;
  Event::MockDispatcher dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  MockClusterInfoFactory test_factory_;

  std::unique_ptr<Upstream::HdsDelegate> hds_delegate_;
  HdsDelegateFriend hds_delegate_friend_;

  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;
  Event::MockTimer* server_response_timer_;
  Event::TimerCb server_response_timer_cb_;

  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_{
      new NiceMock<Upstream::MockClusterInfo>()};
  std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier> message;
  Grpc::MockAsyncStream async_stream_;
  Grpc::MockAsyncClient* async_client_;
  Runtime::MockLoader runtime_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Envoy::AccessLog::MockAccessLogManager> log_manager_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ThreadLocal::MockInstance> tls_;
};

// Test that HdsDelegate builds and sends initial message correctly
TEST_F(HdsTest, HealthCheckRequest) {
  envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse request;
  request.mutable_health_check_request()->mutable_node()->set_id("hds-node");
  request.mutable_health_check_request()->mutable_capability()->add_health_check_protocols(
      envoy::service::health::v3::Capability::HTTP);
  request.mutable_health_check_request()->mutable_capability()->add_health_check_protocols(
      envoy::service::health::v3::Capability::TCP);

  EXPECT_CALL(local_info_, node()).WillOnce(ReturnRef(node_));
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(Grpc::ProtoBufferEq(request), false));
  createHdsDelegate();
}

// Test if processMessage processes endpoints from a HealthCheckSpecifier
// message correctly
TEST_F(HdsTest, TestProcessMessageEndpoints) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  // - Cluster "anna0" with 3 endpoints
  // - Cluster "anna1" with 3 endpoints
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  for (int i = 0; i < 2; i++) {
    auto* health_check = message->add_cluster_health_checks();
    health_check->set_cluster_name("anna" + std::to_string(i));
    for (int j = 0; j < 3; j++) {
      auto* address = health_check->add_locality_endpoints()->add_endpoints()->mutable_address();
      address->mutable_socket_address()->set_address("127.0.0." + std::to_string(i));
      address->mutable_socket_address()->set_port_value(1234 + j);
    }
  }

  // Process message
  EXPECT_CALL(test_factory_, createClusterInfo(_)).Times(2).WillRepeatedly(Return(cluster_info_));
  hds_delegate_friend_.processPrivateMessage(*hds_delegate_, std::move(message));

  // Check Correctness
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 3; j++) {
      auto& host =
          hds_delegate_->hdsClusters()[i]->prioritySet().hostSetsPerPriority()[0]->hosts()[j];
      EXPECT_EQ(host->address()->ip()->addressAsString(), "127.0.0." + std::to_string(i));
      EXPECT_EQ(host->address()->ip()->port(), 1234 + j);
    }
  }
}

// Test if processMessage processes health checks from a HealthCheckSpecifier
// message correctly
TEST_F(HdsTest, TestProcessMessageHealthChecks) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  // - Cluster "minkowski0" with 2 health_checks
  // - Cluster "minkowski1" with 3 health_checks
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  for (int i = 0; i < 2; i++) {
    auto* health_check = message->add_cluster_health_checks();
    health_check->set_cluster_name("minkowski" + std::to_string(i));
    for (int j = 0; j < i + 2; j++) {
      auto hc = health_check->add_health_checks();
      hc->mutable_timeout()->set_seconds(i);
      hc->mutable_interval()->set_seconds(j);
      hc->mutable_unhealthy_threshold()->set_value(j + 1);
      hc->mutable_healthy_threshold()->set_value(j + 1);
      hc->mutable_grpc_health_check();
      hc->mutable_http_health_check()->set_codec_client_type(envoy::type::v3::HTTP1);
      hc->mutable_http_health_check()->set_path("/healthcheck");
    }
  }

  // Process message
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillRepeatedly(Return(cluster_info_));

  hds_delegate_friend_.processPrivateMessage(*hds_delegate_, std::move(message));

  // Check Correctness
  EXPECT_EQ(hds_delegate_->hdsClusters()[0]->healthCheckers().size(), 2);
  EXPECT_EQ(hds_delegate_->hdsClusters()[1]->healthCheckers().size(), 3);
}

// Test if processMessage exits gracefully upon receiving a malformed message
TEST_F(HdsTest, TestProcessMessageMissingFields) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(createSimpleMessage());
  // remove healthy threshold field to create an error
  message->mutable_cluster_health_checks(0)->mutable_health_checks(0)->clear_healthy_threshold();

  // call onReceiveMessage function for testing. Should increment stat_ errors upon
  // getting a bad message
  hds_delegate_->onReceiveMessage(std::move(message));

  // Ensure that we never enabled the response timer that would start health checks,
  // since this config was invalid.
  EXPECT_FALSE(server_response_timer_->enabled_);

  // ensure that no partial information was stored in hds_clusters_
  EXPECT_TRUE(hds_delegate_->hdsClusters().empty());

  // Check Correctness by verifying one request and one error has been generated in stat_
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).errors_.value(), 1);
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).requests_.value(), 1);
}

// Test if processMessage exits gracefully upon receiving a malformed message
// There was a previous valid config, so we go back to that.
TEST_F(HdsTest, TestProcessMessageMissingFieldsWithFallback) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(createSimpleMessage());

  Network::MockClientConnection* connection_ = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillRepeatedly(Return(connection_));
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(2);
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillOnce(Return(cluster_info_));
  EXPECT_CALL(*connection_, setBufferLimits(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Create a invalid message
  message.reset(createSimpleMessage());

  // set this address to be distinguishable from the previous message in sendResponse()
  message->mutable_cluster_health_checks(0)
      ->mutable_locality_endpoints(0)
      ->mutable_endpoints(0)
      ->mutable_address()
      ->mutable_socket_address()
      ->set_address("9.9.9.9");

  // remove healthy threshold field to create an error
  message->mutable_cluster_health_checks(0)->mutable_health_checks(0)->clear_healthy_threshold();

  // Pass invalid message through. Should increment stat_ errors upon
  // getting a bad message.
  hds_delegate_->onReceiveMessage(std::move(message));

  // Ensure that the timer is enabled since there was a previous valid specifier.
  EXPECT_TRUE(server_response_timer_->enabled_);

  // read the response and check that it is pinging the old
  // address 127.0.0.0 instead of the new 9.9.9.9
  auto response = hds_delegate_->sendResponse();
  EXPECT_EQ(response.endpoint_health_response()
                .endpoints_health(0)
                .endpoint()
                .address()
                .socket_address()
                .address(),
            "127.0.0.0");

  // Check Correctness by verifying one request and one error has been generated in stat_
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).errors_.value(), 1);
  EXPECT_EQ(hds_delegate_friend_.getStats(*hds_delegate_).requests_.value(), 2);
}

// Tests OnReceiveMessage given a minimal HealthCheckSpecifier message
TEST_F(HdsTest, TestMinimalOnReceiveMessage) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  // Process message
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  hds_delegate_->onReceiveMessage(std::move(message));
}

// Tests OnReceiveMessage given a HealthCheckSpecifier message without interval field
TEST_F(HdsTest, TestDefaultIntervalOnReceiveMessage) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  // notice that interval field is intentionally left undefined

  // Process message
  EXPECT_CALL(*server_response_timer_, enableTimer(std::chrono::milliseconds(1000), _))
      .Times(AtLeast(1));
  hds_delegate_->onReceiveMessage(std::move(message));
}

// Tests that SendResponse responds to the server in a timely fashion
// given a minimal HealthCheckSpecifier message
TEST_F(HdsTest, TestMinimalSendResponse) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message = std::make_unique<envoy::service::health::v3::HealthCheckSpecifier>();
  message->mutable_interval()->set_seconds(1);

  // Process message and send 2 responses
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(AtLeast(1));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _)).Times(2);
  hds_delegate_->onReceiveMessage(std::move(message));
  hds_delegate_->sendResponse();
  server_response_timer_cb_();
}

TEST_F(HdsTest, TestStreamConnectionFailure) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(&async_stream_));

  EXPECT_CALL(random_, random()).WillOnce(Return(1000005)).WillRepeatedly(Return(654321));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(5), _));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(321), _));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(2321), _));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(6321), _));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(14321), _));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));

  // Test connection failure and retry
  createHdsDelegate();
  retry_timer_cb_();
  retry_timer_cb_();
  retry_timer_cb_();
  retry_timer_cb_();
  retry_timer_cb_();
}

// TODO(lilika): Add unit tests for HdsDelegate::sendResponse() with healthy and
// unhealthy endpoints.

// Tests that SendResponse responds to the server correctly given
// a HealthCheckSpecifier message that contains a single endpoint
// which times out
TEST_F(HdsTest, TestSendResponseOneEndpointTimeout) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(createSimpleMessage());

  Network::MockClientConnection* connection_ = new NiceMock<Network::MockClientConnection>();
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillRepeatedly(Return(connection_));
  EXPECT_CALL(*server_response_timer_, enableTimer(_, _)).Times(2);
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, false));
  EXPECT_CALL(test_factory_, createClusterInfo(_)).WillOnce(Return(cluster_info_));
  EXPECT_CALL(*connection_, setBufferLimits(_));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Send Response
  auto msg = hds_delegate_->sendResponse();

  // Correctness
  EXPECT_EQ(msg.endpoint_health_response().endpoints_health(0).health_status(),
            envoy::config::core::v3::UNHEALTHY);
  EXPECT_EQ(msg.endpoint_health_response()
                .endpoints_health(0)
                .endpoint()
                .address()
                .socket_address()
                .address(),
            "127.0.0.0");
  EXPECT_EQ(msg.endpoint_health_response()
                .endpoints_health(0)
                .endpoint()
                .address()
                .socket_address()
                .port_value(),
            1234);
}

} // namespace Upstream
} // namespace Envoy
