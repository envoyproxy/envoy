#include <cstdint>
#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/filter/tcp_proxy.h"
#include "common/network/address_impl.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Filter {

TEST(TcpProxyConfigTest, NoRouteConfig) {
  std::string json = R"EOF(
    {
      "stat_prefix": "name"
    }
    )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  EXPECT_THROW(TcpProxyConfig(*config, cluster_manager,
                              cluster_manager.thread_local_cluster_.cluster_.info_->stats_store_),
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

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  EXPECT_CALL(cluster_manager, get("fake_cluster")).WillOnce(Return(nullptr));
  EXPECT_THROW(TcpProxyConfig(*config, cluster_manager,
                              cluster_manager.thread_local_cluster_.cluster_.info_->stats_store_),
               EnvoyException);
}

TEST(TcpProxyConfigTest, BadTcpProxyConfig) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": 1,
    "route_config": {
      "routes": [
        {
          "cluster": "fake_cluster"
        }
      ]
    }
   }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  EXPECT_THROW(TcpProxyConfig(*json_config, cluster_manager,
                              cluster_manager.thread_local_cluster_.cluster_.info_->stats_store_),
               Json::Exception);
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
            "destination_ip_list": [
              "::1/128",
              "2001:abcd::/64"
            ],
            "cluster": "with_v6_destination"
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
              "2002::/32"
            ],
            "source_ip_list": [
              "2003::/64"
            ],
            "cluster": "with_v6_source_and_destination"
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json);
  NiceMock<Upstream::MockClusterManager> cm_;

  TcpProxyConfig config_obj(*json_config, cm_,
                            cm_.thread_local_cluster_.cluster_.info_->stats_store_);

  {
    // hit route with destination_ip (10.10.10.10/32)
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("10.10.10.10");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("10.10.10.11");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv4Instance remote_address("0.0.0.0");
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (10.10.11.0/24)
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("10.10.11.11");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("10.10.12.12");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv4Instance remote_address("0.0.0.0");
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (10.11.0.0/16)
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("10.11.11.11");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("10.12.12.12");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv4Instance remote_address("0.0.0.0");
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (11.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("11.11.11.11");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("12.12.12.12");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv4Instance remote_address("0.0.0.0");
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (128.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("128.255.255.255");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination port range
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("1.2.3.4", 12345);
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    EXPECT_EQ(std::string("with_destination_ports"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("1.2.3.4", 23456);
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv4Instance remote_address("0.0.0.0");
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with source port range
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("1.2.3.4", 23456);
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv4Instance remote_address("0.0.0.0", 23459);
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("with_source_ports"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("1.2.3.4", 23456);
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv4Instance remote_address("0.0.0.0", 23458);
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit the route with all criterias present
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("10.0.0.0", 10000);
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv4Instance remote_address("20.0.0.0", 20000);
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("with_everything"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv4Instance local_address("10.0.0.0", 10000);
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv4Instance remote_address("30.0.0.0", 20000);
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (::1/128)
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv6Instance local_address("::1");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    EXPECT_EQ(std::string("with_v6_destination"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (2001:abcd/64")
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv6Instance local_address("2001:abcd:0:0:1::");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    EXPECT_EQ(std::string("with_v6_destination"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip ("2002::/32") and source_ip ("2003::/64")
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv6Instance local_address("2002:0:0:0:0:0::1");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv6Instance remote_address("2003:0:0:0:0::5");
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
    EXPECT_EQ(std::string("with_v6_source_and_destination"),
              config_obj.getRouteFromEntries(connection));
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    Network::Address::Ipv6Instance local_address("2004::");
    EXPECT_CALL(connection, localAddress()).WillRepeatedly(ReturnRef(local_address));
    Network::Address::Ipv6Instance remote_address("::");
    EXPECT_CALL(connection, remoteAddress()).WillRepeatedly(ReturnRef(remote_address));
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json);
  NiceMock<Upstream::MockClusterManager> cm_;

  TcpProxyConfig config_obj(*json_config, cm_,
                            cm_.thread_local_cluster_.cluster_.info_->stats_store_);

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

    Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
    config_.reset(
        new TcpProxyConfig(*config, cluster_manager_,
                           cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_));
  }

  void setup(bool return_connection) {
    if (return_connection) {
      connect_timer_ = new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
      EXPECT_CALL(*connect_timer_, enableTimer(_));

      upstream_connection_ = new NiceMock<Network::MockClientConnection>();
      Upstream::MockHost::MockCreateConnectionData conn_info;
      conn_info.connection_ = upstream_connection_;
      conn_info.host_description_ = Upstream::makeTestHost(
          cluster_manager_.thread_local_cluster_.cluster_.info_, "tcp://127.0.0.1:80");
      EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster", _))
          .WillOnce(Return(conn_info));
      EXPECT_CALL(*upstream_connection_, addReadFilter(_))
          .WillOnce(SaveArg<0>(&upstream_read_filter_));
    } else {
      Upstream::MockHost::MockCreateConnectionData conn_info;
      EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster", _))
          .WillOnce(Return(conn_info));
    }

    filter_.reset(new TcpProxy(config_, cluster_manager_));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    EXPECT_EQ(return_connection ? Network::FilterStatus::Continue
                                : Network::FilterStatus::StopIteration,
              filter_->onNewConnection());

    EXPECT_EQ(Optional<uint64_t>(), filter_->computeHashKey());
    EXPECT_EQ(&filter_callbacks_.connection_, filter_->downstreamConnection());
  }

  TcpProxyConfigSharedPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Network::MockClientConnection>* upstream_connection_{};
  Network::ReadFilterSharedPtr upstream_read_filter_;
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
  upstream_connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response)));
  upstream_read_filter_->onData(response);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpProxyTest, UpstreamDisconnectDownstreamFlowControl) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(*connect_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response)));
  upstream_read_filter_->onData(response);

  EXPECT_CALL(*upstream_connection_, readDisable(true));
  filter_callbacks_.connection_.runHighWatermarkCallbacks();

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  filter_callbacks_.connection_.runLowWatermarkCallbacks();
}

TEST_F(TcpProxyTest, DownstreamDisconnectRemote) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(*connect_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response)));
  upstream_read_filter_->onData(response);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpProxyTest, DownstreamDisconnectLocal) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(*connect_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response)));
  upstream_read_filter_->onData(response);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(TcpProxyTest, UpstreamConnectTimeout) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  connect_timer_->callback_();
  EXPECT_EQ(1U, cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_cx_connect_timeout")
                    .value());
}

TEST_F(TcpProxyTest, NoHost) {
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  setup(false);
}

TEST_F(TcpProxyTest, DisconnectBeforeData) {
  filter_.reset(new TcpProxy(config_, cluster_manager_));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpProxyTest, UpstreamConnectFailure) {
  setup(true);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*connect_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(1U, cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_cx_connect_fail")
                    .value());
}

TEST_F(TcpProxyTest, UpstreamConnectionLimit) {
  cluster_manager_.thread_local_cluster_.cluster_.info_->resource_manager_.reset(
      new Upstream::ResourceManagerImpl(runtime_, "fake_key", 0, 0, 0, 0));

  // setup sets up expectation for tcpConnForCluster but this test is expected to NOT call that
  filter_.reset(new TcpProxy(config_, cluster_manager_));
  // The downstream connection closes if the proxy can't make an upstream connection.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
  filter_->onNewConnection();

  EXPECT_EQ(1U, cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_cx_overflow")
                    .value());
}

class TcpProxyRoutingTest : public testing::Test {
public:
  TcpProxyRoutingTest() {
    std::string json = R"EOF(
    {
      "stat_prefix": "name",
      "route_config": {
        "routes": [
          {
            "destination_ports": "1-9999",
            "cluster": "fake_cluster"
          }
        ]
      }
    }
    )EOF";

    Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
    config_.reset(
        new TcpProxyConfig(*config, cluster_manager_,
                           cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_));
  }

  void setup() {
    EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

    filter_.reset(new TcpProxy(config_, cluster_manager_));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  TcpProxyConfigSharedPtr config_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  std::unique_ptr<TcpProxy> filter_;
};

TEST_F(TcpProxyRoutingTest, NonRoutableConnection) {
  uint32_t total_cx = config_->stats().downstream_cx_total_.value();
  uint32_t non_routable_cx = config_->stats().downstream_cx_no_route_.value();

  setup();

  // port 10000 is outside the specified destination port range
  Network::Address::Ipv4Instance local_address("1.2.3.4", 10000);
  EXPECT_CALL(connection_, localAddress()).WillRepeatedly(ReturnRef(local_address));

  // Expect filter to stop iteration and close connection
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  EXPECT_EQ(total_cx + 1, config_->stats().downstream_cx_total_.value());
  EXPECT_EQ(non_routable_cx + 1, config_->stats().downstream_cx_no_route_.value());
}

TEST_F(TcpProxyRoutingTest, RoutableConnection) {
  uint32_t total_cx = config_->stats().downstream_cx_total_.value();
  uint32_t non_routable_cx = config_->stats().downstream_cx_no_route_.value();

  setup();

  // port 9999 is within the specified destination port range
  Network::Address::Ipv4Instance local_address("1.2.3.4", 9999);
  EXPECT_CALL(connection_, localAddress()).WillRepeatedly(ReturnRef(local_address));

  // Expect filter to try to open a connection to specified cluster
  EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster", _));

  filter_->onNewConnection();

  EXPECT_EQ(total_cx + 1, config_->stats().downstream_cx_total_.value());
  EXPECT_EQ(non_routable_cx, config_->stats().downstream_cx_no_route_.value());
}

} // namespace Filter
} // namespace Envoy
