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
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Tcp {

class AsyncTcpClientImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  AsyncTcpClientImplTest() {
    cluster_manager_.initializeClusters({"fake_cluster"}, {});
    cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
    client_ = std::make_unique<AsyncTcpClientImpl>(
        dispatcher_, cluster_manager_.thread_local_cluster_, nullptr, false);
    client_->setAsyncTcpClientCallbacks(callbacks_);
  }

  void expectCreateConnection() {
    connection_ = new NiceMock<Network::MockClientConnection>();
    Upstream::MockHost::MockCreateConnectionData conn_info;
    conn_info.connection_ = connection_;
    conn_info.host_description_ = Upstream::makeTestHost(
        std::make_unique<NiceMock<Upstream::MockClusterInfo>>(), "tcp://127.0.0.1:80", simTime());

    EXPECT_CALL(cluster_manager_.thread_local_cluster_, tcpConn_(_)).WillOnce(Return(conn_info));
    EXPECT_CALL(*connection_, connect());
    EXPECT_CALL(*connection_, addReadFilter(_));
    EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::Connected));
    ASSERT_TRUE(client_->connect());
    connection_->raiseEvent(Network::ConnectionEvent::Connected);
    ASSERT_TRUE(client_->connected());
  }

  std::unique_ptr<AsyncTcpClientImpl> client_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Network::MockClientConnection* connection_{};

  NiceMock<Tcp::AsyncClient::MockAsyncTcpClientCallbacks> callbacks_;
};

TEST_F(AsyncTcpClientImplTest, BasicWrite) {
  expectCreateConnection();

  EXPECT_CALL(*connection_, write(BufferStringEqual("test data"), _));
  Buffer::OwnedImpl buff("test data");
  client_->write(buff, false);

  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  client_->close(Network::ConnectionCloseType::NoFlush);
  ASSERT_FALSE(client_->connected());
}

TEST_F(AsyncTcpClientImplTest, WaterMark) {
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

TEST_F(AsyncTcpClientImplTest, NoAvaiableConnection) {
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, tcpConn_(_)).WillOnce(Return(conn_info));
  ASSERT_FALSE(client_->connect());
}

TEST_F(AsyncTcpClientImplTest, TestReadDisable) {
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
  expectCreateConnection();
  EXPECT_CALL(callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::Abort));
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  client_->close(Network::ConnectionCloseType::Abort);
  ASSERT_FALSE(client_->connected());
}

} // namespace Tcp
} // namespace Envoy
