#include "common/buffer/buffer_impl.h"
#include "common/filter/tcp_proxy.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;

namespace Filter {

TEST(TcpProxyConfigTest, NoCluster) {
  std::string json = R"EOF(
    {
      "cluster": "fake_cluster",
      "stat_prefix": "name"
    }
    )EOF";

  Json::StringLoader config(json);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  EXPECT_CALL(cluster_manager, get("fake_cluster")).WillOnce(Return(nullptr));
  EXPECT_THROW(TcpProxyConfig(config, cluster_manager, cluster_manager.cluster_.stats_store_),
               EnvoyException);
}

class TcpProxyTest : public testing::Test {
public:
  TcpProxyTest() {
    std::string json = R"EOF(
    {
      "cluster": "fake_cluster",
      "stat_prefix": "name"
    }
    )EOF";

    Json::StringLoader config(json);
    config_.reset(
        new TcpProxyConfig(config, cluster_manager_, cluster_manager_.cluster_.stats_store_));
  }

  void setup(bool return_connection) {
    if (return_connection) {
      connect_timer_ = new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
      EXPECT_CALL(*connect_timer_, enableTimer(_));

      upstream_connection_ = new NiceMock<Network::MockClientConnection>();
      Upstream::MockHost::MockCreateConnectionData conn_info;
      conn_info.connection_ = upstream_connection_;
      conn_info.host_.reset(
          new Upstream::HostImpl(cluster_manager_.cluster_, "tcp://127.0.0.1:80", false, 1, ""));
      EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster")).WillOnce(Return(conn_info));
      EXPECT_CALL(*upstream_connection_, addReadFilter(_))
          .WillOnce(SaveArg<0>(&upstream_read_filter_));
    } else {
      Upstream::MockHost::MockCreateConnectionData conn_info;
      EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster")).WillOnce(Return(conn_info));
    }

    filter_.reset(new TcpProxy(config_, cluster_manager_));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  TcpProxyConfigPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Network::MockClientConnection>* upstream_connection_{};
  Network::ReadFilterPtr upstream_read_filter_;
  NiceMock<Event::MockTimer>* connect_timer_{};
  std::unique_ptr<TcpProxy> filter_;
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(TcpProxyTest, UpstreamDisconnect) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(*connect_timer_, disableTimer());
  upstream_connection_->raiseEvents(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response)));
  upstream_read_filter_->onData(response);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  upstream_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpProxyTest, DowntsreamDisconnect) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(*connect_timer_, disableTimer());
  upstream_connection_->raiseEvents(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response)));
  upstream_read_filter_->onData(response);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpProxyTest, UpstreamConnectTimeout) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  connect_timer_->callback_();
  EXPECT_EQ(1U, cluster_manager_.cluster_.stats_store_
                    .counter("cluster.fake_cluster.upstream_cx_connect_timeout")
                    .value());
}

TEST_F(TcpProxyTest, NoHost) {
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  setup(false);
}

TEST_F(TcpProxyTest, DisconnectBeforeData) {
  filter_.reset(new TcpProxy(config_, cluster_manager_));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpProxyTest, UpstreamConnectFailure) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*connect_timer_, disableTimer());
  upstream_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(1U, cluster_manager_.cluster_.stats_store_
                    .counter("cluster.fake_cluster.upstream_cx_connect_fail")
                    .value());
}

TEST_F(TcpProxyTest, UpstreamConnectionLimit) {
  cluster_manager_.cluster_.resource_manager_.reset(
      new Upstream::ResourceManagerImpl(runtime_, "fake_key", 0, 0, 0, 0));

  // setup sets up expectation for tcpConnForCluster but this test is expected to NOT call that
  filter_.reset(new TcpProxy(config_, cluster_manager_));
  // The downstream connection closes if the proxy can't make an upstream connection.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  EXPECT_EQ(
      1U,
      cluster_manager_.cluster_.stats_store_.counter("cluster.fake_cluster.upstream_cx_overflow")
          .value());
}

} // Filter
