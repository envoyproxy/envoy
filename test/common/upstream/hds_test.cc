#include "envoy/service/discovery/v2/hds.pb.h"

#include "common/ssl/context_manager_impl.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/health_discovery_service.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

using ::testing::AtLeast;

namespace Envoy {
namespace Upstream {

class HdsTest : public testing::Test {
public:
  HdsTest()
      : retry_timer_(new Event::MockTimer()), server_response_timer_(new Event::MockTimer()),
        async_client_(new Grpc::MockAsyncClient()) {
    node_.set_id("foo");
  }

  void createHdsDelegate() {
    InSequence s;
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      retry_timer_cb_ = timer_cb;
      return retry_timer_;
    }));
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      server_response_timer_cb_ = timer_cb;
      return server_response_timer_;
    }));
    hds_delegate_.reset(new HdsDelegate(node_, stats_store_, Grpc::AsyncClientPtr(async_client_),
                                        dispatcher_, runtime_, stats_store_, ssl_context_manager_,
                                        secret_manager_, random_, test_factory_));
  }

  envoy::api::v2::core::Node node_;
  Event::MockDispatcher dispatcher_;

  Stats::IsolatedStoreImpl stats_store_;

  HdsDelegatePtr hds_delegate_;
  MockClusterInfoFactory test_factory_;

  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;
  Event::MockTimer* server_response_timer_;
  Event::TimerCb server_response_timer_cb_;

  ClusterInfoConstSharedPtr info_;
  std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier> message;
  Grpc::MockAsyncStream async_stream_;
  Grpc::MockAsyncClient* async_client_;
  Runtime::MockLoader runtime_;
  Ssl::ContextManagerImpl ssl_context_manager_{runtime_};
  Secret::MockSecretManager secret_manager_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

TEST_F(HdsTest, TestProcessMessageEndpoints) {
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessage(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(new envoy::service::discovery::v2::HealthCheckSpecifier);
  message->mutable_interval()->set_seconds(1);

  auto* health_check = message->add_health_check();
  health_check->set_cluster_name("anna");

  auto* socket_address =
      health_check->add_endpoints()->add_endpoints()->mutable_address()->mutable_socket_address();
  socket_address->set_address("127.0.0.0");
  socket_address->set_port_value(1234);

  auto* socket_address2 = health_check->mutable_endpoints(0)
                              ->add_endpoints()
                              ->mutable_address()
                              ->mutable_socket_address();
  socket_address2->set_address("127.0.0.1");
  socket_address2->set_port_value(2345);

  auto* socket_address3 =
      health_check->add_endpoints()->add_endpoints()->mutable_address()->mutable_socket_address();
  socket_address3->set_address("127.0.1.0");
  socket_address3->set_port_value(8765);

  auto* health_check2 = message->add_health_check();
  health_check2->set_cluster_name("voronoi");

  auto* socket_address4 =
      health_check2->add_endpoints()->add_endpoints()->mutable_address()->mutable_socket_address();
  socket_address4->set_address("128.0.0.0");
  socket_address4->set_port_value(1234);

  auto* socket_address5 = health_check2->mutable_endpoints(0)
                              ->add_endpoints()
                              ->mutable_address()
                              ->mutable_socket_address();
  socket_address5->set_address("128.0.0.1");
  socket_address5->set_port_value(2345);

  auto* socket_address6 =
      health_check2->add_endpoints()->add_endpoints()->mutable_address()->mutable_socket_address();
  socket_address6->set_address("128.0.1.0");
  socket_address6->set_port_value(8765);

  // Process message
  EXPECT_CALL(test_factory_, createClusterInfo(_, _, _, _, _, _, _)).Times(2);
  hds_delegate_->processMessage(std::move(message));

  // Check Correctness
  auto& host = hds_delegate_->hdsClusters()[0]->prioritySet().hostSetsPerPriority()[0]->hosts()[0];
  EXPECT_EQ(host->address()->ip()->addressAsString(), "127.0.0.0");
  EXPECT_EQ(host->address()->ip()->port(), 1234);

  auto& host2 = hds_delegate_->hdsClusters()[0]->prioritySet().hostSetsPerPriority()[0]->hosts()[1];
  EXPECT_EQ(host2->address()->ip()->addressAsString(), "127.0.0.1");
  EXPECT_EQ(host2->address()->ip()->port(), 2345);

  auto& host3 = hds_delegate_->hdsClusters()[0]->prioritySet().hostSetsPerPriority()[0]->hosts()[2];
  EXPECT_EQ(host3->address()->ip()->addressAsString(), "127.0.1.0");
  EXPECT_EQ(host3->address()->ip()->port(), 8765);

  auto& host4 = hds_delegate_->hdsClusters()[1]->prioritySet().hostSetsPerPriority()[0]->hosts()[0];
  EXPECT_EQ(host4->address()->ip()->addressAsString(), "128.0.0.0");
  EXPECT_EQ(host4->address()->ip()->port(), 1234);

  auto& host5 = hds_delegate_->hdsClusters()[1]->prioritySet().hostSetsPerPriority()[0]->hosts()[1];
  EXPECT_EQ(host5->address()->ip()->addressAsString(), "128.0.0.1");
  EXPECT_EQ(host5->address()->ip()->port(), 2345);

  auto& host6 = hds_delegate_->hdsClusters()[1]->prioritySet().hostSetsPerPriority()[0]->hosts()[2];
  EXPECT_EQ(host6->address()->ip()->addressAsString(), "128.0.1.0");
  EXPECT_EQ(host6->address()->ip()->port(), 8765);
}

TEST_F(HdsTest, TestProcessMessageHealthChecks) {
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessage(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(new envoy::service::discovery::v2::HealthCheckSpecifier);
  message->mutable_interval()->set_seconds(1);

  auto* health_check = message->add_health_check();
  health_check->set_cluster_name("anna");

  health_check->add_health_checks()->mutable_timeout()->set_seconds(1);
  health_check->mutable_health_checks(0)->mutable_interval()->set_seconds(1);
  health_check->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(2);
  health_check->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(2);
  health_check->mutable_health_checks(0)->mutable_grpc_health_check();
  health_check->mutable_health_checks(0)->mutable_http_health_check()->set_use_http2(false);
  health_check->mutable_health_checks(0)->mutable_http_health_check()->set_path("/healthcheck");

  health_check->add_health_checks()->mutable_timeout()->set_seconds(1);
  health_check->mutable_health_checks(1)->mutable_interval()->set_seconds(1);
  health_check->mutable_health_checks(1)->mutable_unhealthy_threshold()->set_value(2);
  health_check->mutable_health_checks(1)->mutable_healthy_threshold()->set_value(2);
  health_check->mutable_health_checks(1)->mutable_grpc_health_check();
  health_check->mutable_health_checks(1)->mutable_http_health_check()->set_use_http2(false);
  health_check->mutable_health_checks(1)->mutable_http_health_check()->set_path("/healthcheck");

  auto* health_check2 = message->add_health_check();
  health_check2->set_cluster_name("minkowski");

  health_check2->add_health_checks()->mutable_timeout()->set_seconds(2);
  health_check2->mutable_health_checks(0)->mutable_interval()->set_seconds(2);
  health_check2->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(4);
  health_check2->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(21);
  health_check2->mutable_health_checks(0)->mutable_grpc_health_check();
  health_check2->mutable_health_checks(0)->mutable_http_health_check()->set_use_http2(true);
  health_check2->mutable_health_checks(0)->mutable_http_health_check()->set_path("/healthcheck2");

  // Process message
  info_.reset(new NiceMock<Upstream::MockClusterInfo>());
  EXPECT_CALL(test_factory_, createClusterInfo(_, _, _, _, _, _, _)).WillRepeatedly(Return(info_));
  hds_delegate_->processMessage(std::move(message));

  // Check Correctness
  EXPECT_EQ(hds_delegate_->healthCheckers().size(), 3);
}

TEST_F(HdsTest, TestMinimalOnReceiveMessage) {
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessage(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(new envoy::service::discovery::v2::HealthCheckSpecifier);
  message->mutable_interval()->set_seconds(1);

  EXPECT_CALL(*server_response_timer_, enableTimer(_)).Times(AtLeast(1));
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
}

TEST_F(HdsTest, TestMinimalSendResponse) {
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessage(_, _));
  createHdsDelegate();

  // Create Message
  message.reset(new envoy::service::discovery::v2::HealthCheckSpecifier);
  message->mutable_interval()->set_seconds(1);

  EXPECT_CALL(*server_response_timer_, enableTimer(_)).Times(AtLeast(1));
  EXPECT_CALL(async_stream_, sendMessage(_, _)).Times(2);
  // Process message
  hds_delegate_->onReceiveMessage(std::move(message));
  hds_delegate_->sendResponse();
  server_response_timer_cb_();
}

TEST_F(HdsTest, TestStreamConnectionFailure) {
  EXPECT_CALL(*async_client_, start(_, _))
      .WillOnce(Return(nullptr))
      .WillOnce(Return(&async_stream_));
  EXPECT_CALL(*retry_timer_, enableTimer(_));
  EXPECT_CALL(async_stream_, sendMessage(_, _));

  createHdsDelegate();
  retry_timer_cb_();
}

} // namespace Upstream
} // namespace Envoy
