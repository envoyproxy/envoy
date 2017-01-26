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
using testing::ReturnRefOfCopy;
using testing::SaveArg;

namespace Filter {

TEST(TcpProxyConfigTest, NoRouteConfig) {
  std::string json = R"EOF(
    {
      "stat_prefix": "name"
    }
    )EOF";

  Json::ObjectPtr config = Json::Factory::LoadFromString(json);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  EXPECT_THROW(
      TcpProxyConfig(*config, cluster_manager, cluster_manager.cluster_.info_->stats_store_),
      EnvoyException);
}

TEST(TcpProxyConfigTest, NoCluster) {
  std::string json = R"EOF(
    {
      "stat_prefix": "name",
      "route_config": {
        "routes": [
          {
            "cluster": "fake_cluster"
          }
        ]
      }
    }
    )EOF";

  Json::ObjectPtr config = Json::Factory::LoadFromString(json);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  EXPECT_CALL(cluster_manager, get("fake_cluster")).WillOnce(Return(nullptr));
  EXPECT_THROW(
      TcpProxyConfig(*config, cluster_manager, cluster_manager.cluster_.info_->stats_store_),
      EnvoyException);
}

TEST(TcpProxyConfigTest, Routes) {
  std::string json = R"EOF(
    {
      "stat_prefix": "name",
      "route_config": {
        "routes": [
          {
            "destination_ip_list": [
              "10.10.10.10/32",
              "10.10.11.0/24",
              "10.11.0.0/16",
              "11.0.0.0/8",
              "128.0.0.0/1"
            ],
            "cluster": "with_destination_ip_list"
          },
          {
            "destination_ports": "1-1024,2048-4096,12345", 
            "cluster": "with_destination_ports"
          },
          {  
            "source_ports": "23457,23459", 
            "cluster": "with_source_ports"
          },
          {
            "destination_ip_list": [
              "10.0.0.0/24"
            ],
            "source_ip_list": [
              "20.0.0.0/24"
            ],
            "destination_ports" : "10000",
            "source_ports": "20000",
            "cluster": "with_everything"
          },
          {
            "cluster": "catch_all"
          }
        ]
      }
    }
    )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json);
  NiceMock<Upstream::MockClusterManager> cm_;

  // The TcpProxyConfig constructor checks if the clusters mentioned in the route_config are valid.
  // We need to make sure to return a non-null pointer for each, otherwise the constructor will
  // throw an exception and fail.
  EXPECT_CALL(cm_, get("with_destination_ip_list")).WillRepeatedly(Return(cm_.cluster_.info_));
  EXPECT_CALL(cm_, get("with_destination_ports")).WillRepeatedly(Return(cm_.cluster_.info_));
  EXPECT_CALL(cm_, get("with_source_ports")).WillRepeatedly(Return(cm_.cluster_.info_));
  EXPECT_CALL(cm_, get("with_everything")).WillRepeatedly(Return(cm_.cluster_.info_));
  EXPECT_CALL(cm_, get("catch_all")).WillRepeatedly(Return(cm_.cluster_.info_));

  TcpProxyConfig config_obj(*json_config, cm_, cm_.cluster_.info_->stats_store_);

  {
    // hit route with destination_ip (10.10.10.10/32)
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://10.10.10.10:0"));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://10.10.10.11:0"));
    EXPECT_CALL(connection, remoteAddress())
        .WillRepeatedly(ReturnRefOfCopy(std::string("tcp://0.0.0.0:0")));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (10.10.11.0/24)
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://10.10.11.11:0"));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://10.10.12.12:0"));
    EXPECT_CALL(connection, remoteAddress())
        .WillRepeatedly(ReturnRefOfCopy(std::string("tcp://0.0.0.0:0")));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (10.11.0.0/16)
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://10.11.11.11:0"));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://10.12.12.12:0"));
    EXPECT_CALL(connection, remoteAddress())
        .WillRepeatedly(ReturnRefOfCopy(std::string("tcp://0.0.0.0:0")));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (11.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://11.11.11.11:0"));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://12.12.12.12:0"));
    EXPECT_CALL(connection, remoteAddress())
        .WillRepeatedly(ReturnRefOfCopy(std::string("tcp://0.0.0.0:0")));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (128.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://128.255.255.255:0"));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination port range
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://1.2.3.4:12345"));
    EXPECT_EQ(std::string("with_destination_ports"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://1.2.3.4:23456"));
    EXPECT_CALL(connection, remoteAddress())
        .WillRepeatedly(ReturnRefOfCopy(std::string("tcp://0.0.0.0:0")));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with source port range
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://1.2.3.4:23456"));
    EXPECT_CALL(connection, remoteAddress())
        .WillRepeatedly(ReturnRefOfCopy(std::string("tcp://0.0.0.0:23459")));
    EXPECT_EQ(std::string("with_source_ports"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://1.2.3.4:23456"));
    EXPECT_CALL(connection, remoteAddress())
        .WillRepeatedly(ReturnRefOfCopy(std::string("tcp://0.0.0.0:23458")));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit the route with all criterias present
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://10.0.0.0:10000"));
    EXPECT_CALL(connection, remoteAddress())
        .WillRepeatedly(ReturnRefOfCopy(std::string("tcp://20.0.0.0:20000")));
    EXPECT_EQ(std::string("with_everything"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, destinationAddress()).WillRepeatedly(Return("tcp://10.0.0.0:10000"));
    EXPECT_CALL(connection, remoteAddress())
        .WillRepeatedly(ReturnRefOfCopy(std::string("tcp://30.0.0.0:20000")));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }
}

TEST(TcpProxyConfigTest, EmptyRouteConfig) {
  std::string json = R"EOF(
    {
      "stat_prefix": "name",
      "route_config": {
        "routes": [
        ]
      }
    }
    )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json);
  NiceMock<Upstream::MockClusterManager> cm_;

  TcpProxyConfig config_obj(*json_config, cm_, cm_.cluster_.info_->stats_store_);

  NiceMock<Network::MockConnection> connection;
  EXPECT_EQ(std::string(""), config_obj.getRouteFromEntries(connection));
}

class TcpProxyTest : public testing::Test {
public:
  TcpProxyTest() {
    std::string json = R"EOF(
    {
      "stat_prefix": "name",
      "route_config": {
        "routes": [
          {
            "cluster": "fake_cluster"
          }
        ]
      }
    }
    )EOF";

    Json::ObjectPtr config = Json::Factory::LoadFromString(json);
    config_.reset(new TcpProxyConfig(*config, cluster_manager_,
                                     cluster_manager_.cluster_.info_->stats_store_));
  }

  void setup(bool return_connection) {
    if (return_connection) {
      connect_timer_ = new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
      EXPECT_CALL(*connect_timer_, enableTimer(_));

      upstream_connection_ = new NiceMock<Network::MockClientConnection>();
      Upstream::MockHost::MockCreateConnectionData conn_info;
      conn_info.connection_ = upstream_connection_;
      conn_info.host_.reset(new Upstream::HostImpl(cluster_manager_.cluster_.info_,
                                                   "tcp://127.0.0.1:80", false, 1, ""));
      EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster")).WillOnce(Return(conn_info));
      EXPECT_CALL(*upstream_connection_, addReadFilter(_))
          .WillOnce(SaveArg<0>(&upstream_read_filter_));
    } else {
      Upstream::MockHost::MockCreateConnectionData conn_info;
      EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster")).WillOnce(Return(conn_info));
    }

    filter_.reset(new TcpProxy(config_, cluster_manager_));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    EXPECT_EQ(return_connection ? Network::FilterStatus::Continue
                                : Network::FilterStatus::StopIteration,
              filter_->onNewConnection());
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

TEST_F(TcpProxyTest, DownstreamDisconnectRemote) {
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

TEST_F(TcpProxyTest, DownstreamDisconnectLocal) {
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
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::LocalClose);
}

TEST_F(TcpProxyTest, UpstreamConnectTimeout) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  connect_timer_->callback_();
  EXPECT_EQ(1U, cluster_manager_.cluster_.info_->stats_store_.counter("upstream_cx_connect_timeout")
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
  EXPECT_EQ(1U, cluster_manager_.cluster_.info_->stats_store_.counter("upstream_cx_connect_fail")
                    .value());
}

TEST_F(TcpProxyTest, UpstreamConnectionLimit) {
  cluster_manager_.cluster_.info_->resource_manager_.reset(
      new Upstream::ResourceManagerImpl(runtime_, "fake_key", 0, 0, 0, 0));

  // setup sets up expectation for tcpConnForCluster but this test is expected to NOT call that
  filter_.reset(new TcpProxy(config_, cluster_manager_));
  // The downstream connection closes if the proxy can't make an upstream connection.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
  filter_->onNewConnection();

  EXPECT_EQ(1U,
            cluster_manager_.cluster_.info_->stats_store_.counter("upstream_cx_overflow").value());
}

} // Filter
