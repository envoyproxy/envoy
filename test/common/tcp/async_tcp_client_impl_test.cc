#include "source/common/tcp/async_tcp_client_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Tcp {

class CustomMockClientConnection : public Network::MockClientConnection {
public:
  ~CustomMockClientConnection() {
    if (state_ != Connection::State::Closed) {
      raiseEvent(Network::ConnectionEvent::LocalClose);
    }
  };
};

class AsyncTcpClientImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  AsyncTcpClientImplTest() = default;

  void setUpClient() {
    cluster_manager_.initializeClusters({"fake_cluster"}, {});
    cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
    connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    client_ = std::make_unique<AsyncTcpClientImpl>(
        dispatcher_, cluster_manager_.thread_local_cluster_, nullptr, false);
    client_->setAsyncTcpClientCallbacks(callbacks_);
  }

  void expectCreateConnection(bool trigger_connected = true) {
    connection_ = new NiceMock<CustomMockClientConnection>();
    Upstream::MockHost::MockCreateConnectionData conn_info;
    connection_->streamInfo().setAttemptCount(1);
    conn_info.connection_ = connection_;

    conn_info.host_description_ = Upstream::makeTestHost(
        std::make_unique<NiceMock<Upstream::MockClusterInfo>>(), "tcp://127.0.0.1:80", simTime());

    EXPECT_CALL(cluster_manager_.thread_local_cluster_, tcpConn_(_)).WillOnce(Return(conn_info));
    EXPECT_CALL(*connection_, connect());
    EXPECT_CALL(*connection_, addReadFilter(_));
    if (trigger_connected) {
      EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::Connected));
    }

    ASSERT_TRUE(client_->connect());

    if (trigger_connected) {
      connection_->raiseEvent(Network::ConnectionEvent::Connected);
      ASSERT_TRUE(client_->connected());
    }
  }

  std::unique_ptr<AsyncTcpClientImpl> client_;
  NiceMock<Event::MockTimer>* connect_timer_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  CustomMockClientConnection* connection_{};

  NiceMock<Tcp::AsyncClient::MockAsyncTcpClientCallbacks> callbacks_;
};

TEST_F(AsyncTcpClientImplTest, BasicWrite) {
  setUpClient();
  expectCreateConnection();

  EXPECT_CALL(*connection_, write(BufferStringEqual("test data"), _));
  Buffer::OwnedImpl buff("test data");
  client_->write(buff, false);

  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, RstClose) {
  setUpClient();
  expectCreateConnection();

  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_->detectedCloseType(), Network::DetectedCloseType::LocalReset);
      }));
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).WillOnce(InvokeWithoutArgs([&]() -> void {
    EXPECT_EQ(client_->detectedCloseType(), Network::DetectedCloseType::LocalReset);
  }));
  client_->close(Network::ConnectionCloseType::AbortReset);
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, WaterMark) {
  setUpClient();
  expectCreateConnection();

  EXPECT_CALL(callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(callbacks_, onBelowWriteBufferLowWatermark());
  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();

  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, NoAvailableConnection) {
  setUpClient();
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, tcpConn_(_)).WillOnce(Return(conn_info));
  ASSERT_FALSE(client_->connect());
}

TEST_F(AsyncTcpClientImplTest, TestReadDisable) {
  setUpClient();
  expectCreateConnection();
  EXPECT_CALL(*connection_, readDisable(true));
  client_->readDisable(true);

  EXPECT_CALL(*connection_, readDisable(false));
  client_->readDisable(false);

  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, TestCloseType) {
  setUpClient();
  expectCreateConnection();
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_->detectedCloseType(), Network::DetectedCloseType::Normal);
      }));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::Abort));
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).WillOnce(InvokeWithoutArgs([&]() -> void {
    EXPECT_EQ(client_->detectedCloseType(), Network::DetectedCloseType::Normal);
  }));
  client_->close(Network::ConnectionCloseType::Abort);
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, TestGetDispatcher) {
  setUpClient();
  expectCreateConnection();
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_EQ(&dispatcher_, &client_->dispatcher());
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, TestGetStreamInfo) {
  setUpClient();
  expectCreateConnection();
  EXPECT_TRUE(client_->getStreamInfo().has_value());
  EXPECT_EQ(1, client_->getStreamInfo()->attemptCount());
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, TestGetStreamInfoNullOpt) {
  setUpClient();
  expectCreateConnection();
  EXPECT_TRUE(client_->getStreamInfo().has_value());
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_FALSE(client_->connected());
  ASSERT_FALSE(client_->getStreamInfo().has_value());
}

TEST_F(AsyncTcpClientImplTest, TestTimingStats) {
  setUpClient();
  expectCreateConnection();
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(
      cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_,
      deliverHistogramToSinks(testing::Property(&Stats::Metric::name, "upstream_cx_length_ms"), _));
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, TestCounterStats) {
  setUpClient();
  expectCreateConnection();
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  Buffer::OwnedImpl buff("test data");
  client_->write(buff, false);
  EXPECT_EQ(buff.length(), cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                               ->upstream_cx_tx_bytes_total_.value());
  client_->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_total_.value());
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_destroy_.value());
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_destroy_local_.value());
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, TestFailStats) {
  setUpClient();
  expectCreateConnection(false);
  connect_timer_->invokeCallback();
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_connect_timeout_.value());
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_connect_fail_.value());
}

TEST_F(AsyncTcpClientImplTest, TestFailWithReconnect) {
  setUpClient();
  expectCreateConnection(false);
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  ASSERT_FALSE(client_->connected());
  connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_connect_fail_.value());
  // Reconnect should success without the timer failure.
  client_->setAsyncTcpClientCallbacks(callbacks_);
  expectCreateConnection(true);
}

TEST_F(AsyncTcpClientImplTest, TestCxDestroyRemoteClose) {
  setUpClient();
  expectCreateConnection();
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::RemoteClose));
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_destroy_.value());
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_destroy_remote_.value());
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, TestActiveCx) {
  setUpClient();
  expectCreateConnection();
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_active_.value());
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  connection_->raiseEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(0UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_active_.value());
}

TEST_F(AsyncTcpClientImplTest, TestActiveCxWhileNotConnected) {
  setUpClient();
  expectCreateConnection(false);
  EXPECT_EQ(1UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_active_.value());
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  connection_->raiseEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(0UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_active_.value());
}

TEST_F(AsyncTcpClientImplTest, ReconnectWhileClientConnected) {
  setUpClient();
  expectCreateConnection();
  EXPECT_FALSE(client_->connect());
}

TEST_F(AsyncTcpClientImplTest, ReconnectWhileClientConnecting) {
  setUpClient();
  expectCreateConnection(false);
  EXPECT_FALSE(client_->connect());
}

TEST_F(AsyncTcpClientImplTest, ReconnectAfterClientDisconnected) {
  setUpClient();
  expectCreateConnection();

  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  connection_->raiseEvent(Network::ConnectionEvent::LocalClose);
  connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  expectCreateConnection();

  EXPECT_EQ(2UL, cluster_manager_.thread_local_cluster_.cluster_.info_->traffic_stats_
                     ->upstream_cx_total_.value());
}

} // namespace Tcp
} // namespace Envoy
