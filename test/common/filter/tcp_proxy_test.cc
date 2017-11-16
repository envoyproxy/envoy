#include <cstdint>
#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/config/filter_json.h"
#include "common/filter/tcp_proxy.h"
#include "common/network/address_impl.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::MatchesRegex;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Filter {

namespace {
TcpProxyConfig constructTcpProxyConfigFromJson(const Json::Object& json,
                                               Server::Configuration::FactoryContext& context) {
  envoy::api::v2::filter::network::TcpProxy tcp_proxy;
  Config::FilterJson::translateTcpProxy(json, tcp_proxy);
  return TcpProxyConfig(tcp_proxy, context);
}
} // namespace

TEST(TcpProxyConfigTest, NoRouteConfig) {
  std::string json = R"EOF(
    {
      "stat_prefix": "name"
    }
    )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW(constructTcpProxyConfigFromJson(*config, factory_context), EnvoyException);
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
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_CALL(factory_context.cluster_manager_, get("fake_cluster")).WillOnce(Return(nullptr));
  EXPECT_THROW(constructTcpProxyConfigFromJson(*config, factory_context), EnvoyException);
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
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW(constructTcpProxyConfigFromJson(*json_config, factory_context), Json::Exception);
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
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;

  TcpProxyConfig config_obj(constructTcpProxyConfigFromJson(*json_config, factory_context_));

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
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;

  TcpProxyConfig config_obj(constructTcpProxyConfigFromJson(*json_config, factory_context_));

  NiceMock<Network::MockConnection> connection;
  EXPECT_EQ(std::string(""), config_obj.getRouteFromEntries(connection));
}

TEST(TcpProxyConfigTest, AccessLogConfig) {
  envoy::api::v2::filter::network::TcpProxy config;
  envoy::api::v2::filter::AccessLog* log = config.mutable_access_log()->Add();
  log->set_name(Config::AccessLogNames::get().FILE);
  {
    envoy::api::v2::filter::FileAccessLog file_access_log;
    file_access_log.set_path("some_path");
    file_access_log.set_format("the format specifier");
    ProtobufWkt::Struct* custom_config = log->mutable_config();
    MessageUtil::jsonConvert(file_access_log, *custom_config);
  }

  log = config.mutable_access_log()->Add();
  log->set_name(Config::AccessLogNames::get().FILE);
  {
    envoy::api::v2::filter::FileAccessLog file_access_log;
    file_access_log.set_path("another path");
    ProtobufWkt::Struct* custom_config = log->mutable_config();
    MessageUtil::jsonConvert(file_access_log, *custom_config);
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  TcpProxyConfig config_obj(config, factory_context_);

  EXPECT_EQ(2, config_obj.accessLogs().size());
}

class TcpProxyNoConfigTest : public testing::Test {
public:
  TcpProxyNoConfigTest() {}

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::unique_ptr<TcpProxy> filter_;
};

TEST_F(TcpProxyNoConfigTest, Initialization) {
  filter_.reset(new TcpProxy(nullptr, factory_context_.cluster_manager_));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
}

TEST_F(TcpProxyNoConfigTest, ReadDisableDownstream) {
  filter_.reset(new TcpProxy(nullptr, factory_context_.cluster_manager_));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  filter_->readDisableDownstream(true);
}

class TcpProxyTest : public testing::Test {
public:
  TcpProxyTest() {
    ON_CALL(*factory_context_.access_log_manager_.file_, write(_))
        .WillByDefault(SaveArg<0>(&access_log_data_));
  }

  void configure(const std::string& accessLogJson) {
    std::string json = R"EOF(
    {{
      "stat_prefix": "name",
      "route_config": {{
        "routes": [
          {{
            "cluster": "fake_cluster"
          }}
        ]
      }},
      "access_log": [
        {}
      ]
    }}
    )EOF";

    Json::ObjectSharedPtr config = Json::Factory::loadFromString(fmt::format(json, accessLogJson));
    config_.reset(new TcpProxyConfig(constructTcpProxyConfigFromJson(*config, factory_context_)));
  }

  void setup(bool return_connection, const std::string& accessLogJson) {
    configure(accessLogJson);
    if (return_connection) {
      connect_timer_ = new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
      EXPECT_CALL(*connect_timer_, enableTimer(_));

      upstream_local_address_ = Network::Utility::resolveUrl("tcp://2.2.2.2:50000");
      upstream_connection_ = new NiceMock<Network::MockClientConnection>();
      ON_CALL(*upstream_connection_, localAddress())
          .WillByDefault(ReturnPointee(upstream_local_address_));
      Upstream::MockHost::MockCreateConnectionData conn_info;
      conn_info.connection_ = upstream_connection_;
      conn_info.host_description_ = Upstream::makeTestHost(
          factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          "tcp://127.0.0.1:80");
      EXPECT_CALL(factory_context_.cluster_manager_, tcpConnForCluster_("fake_cluster", _))
          .WillOnce(Return(conn_info));
      EXPECT_CALL(*upstream_connection_, addReadFilter(_))
          .WillOnce(SaveArg<0>(&upstream_read_filter_));
    } else {
      Upstream::MockHost::MockCreateConnectionData conn_info;
      EXPECT_CALL(factory_context_.cluster_manager_, tcpConnForCluster_("fake_cluster", _))
          .WillOnce(Return(conn_info));
    }

    filter_.reset(new TcpProxy(config_, factory_context_.cluster_manager_));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    EXPECT_EQ(return_connection ? Network::FilterStatus::Continue
                                : Network::FilterStatus::StopIteration,
              filter_->onNewConnection());

    EXPECT_EQ(Optional<uint64_t>(), filter_->computeHashKey());
    EXPECT_EQ(&filter_callbacks_.connection_, filter_->downstreamConnection());
  }

  void setup(bool return_connection) { setup(return_connection, std::string()); }

  TcpProxyConfigSharedPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Network::MockClientConnection>* upstream_connection_{};
  Network::ReadFilterSharedPtr upstream_read_filter_;
  NiceMock<Event::MockTimer>* connect_timer_{};
  std::unique_ptr<TcpProxy> filter_;
  std::string access_log_data_;
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
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
  setup(true, R"EOF(
      {
        "path": "unused",
        "format": "%RESPONSE_FLAGS%"
      }
    )EOF");

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  connect_timer_->callback_();
  EXPECT_EQ(1U, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_cx_connect_timeout")
                    .value());

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF");
}

TEST_F(TcpProxyTest, NoHost) {
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  setup(false, R"EOF(
      {
        "path": "unused",
        "format": "%RESPONSE_FLAGS%"
      }
    )EOF");
  filter_.reset();
  EXPECT_EQ(access_log_data_, "UH");
}

TEST_F(TcpProxyTest, DisconnectBeforeData) {
  configure("");
  filter_.reset(new TcpProxy(config_, factory_context_.cluster_manager_));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpProxyTest, UpstreamConnectFailure) {
  setup(true, R"EOF(
      {
        "path": "unused",
        "format": "%RESPONSE_FLAGS%"
      }
    )EOF");

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connection_, write(BufferEqual(&buffer)));
  filter_->onData(buffer);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*connect_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(1U, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_cx_connect_fail")
                    .value());

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF");
}

TEST_F(TcpProxyTest, UpstreamConnectionLimit) {
  configure(R"EOF(
      {
        "path": "unused",
        "format": "%RESPONSE_FLAGS%"
      }
    )EOF");
  factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->resource_manager_.reset(
      new Upstream::ResourceManagerImpl(factory_context_.runtime_loader_, "fake_key", 0, 0, 0, 0));

  // setup sets up expectation for tcpConnForCluster but this test is expected to NOT call that
  filter_.reset(new TcpProxy(config_, factory_context_.cluster_manager_));
  // The downstream connection closes if the proxy can't make an upstream connection.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
  filter_->onNewConnection();

  EXPECT_EQ(1U, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_cx_overflow")
                    .value());

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UO");
}

// Test that access log fields %UPSTREAM_HOST% and %UPSTREAM_CLUSTER% are correctly logged.
TEST_F(TcpProxyTest, AccessLogUpstreamHost) {
  setup(true, R"EOF(
      {
        "path": "unused",
        "format": "%UPSTREAM_HOST% %UPSTREAM_CLUSTER%"
      }
    )EOF");
  filter_.reset();
  EXPECT_EQ(access_log_data_, "127.0.0.1:80 fake_cluster");
}

// Test that access log field %UPSTREAM_LOCAL_ADDRESS% is correctly logged.
TEST_F(TcpProxyTest, AccessLogUpstreamLocalAddress) {
  setup(true, R"EOF(
      {
        "path": "unused",
        "format": "%UPSTREAM_LOCAL_ADDRESS%"
      }
    )EOF");
  filter_.reset();
  EXPECT_EQ(access_log_data_, "2.2.2.2:50000");
}

// Test that access log field %DOWNSTREAM_ADDRESS% is correctly logged.
TEST_F(TcpProxyTest, AccessLogDownstreamAddress) {
  Network::Address::InstanceConstSharedPtr downstream_address =
      Network::Utility::resolveUrl("tcp://1.1.1.1:40000");
  ON_CALL(filter_callbacks_.connection_, remoteAddress())
      .WillByDefault(ReturnPointee(downstream_address));
  setup(true, R"EOF(
      {
        "path": "unused",
        "format": "%DOWNSTREAM_ADDRESS%"
      }
    )EOF");
  filter_.reset();
  EXPECT_EQ(access_log_data_, "1.1.1.1:40000");
}

// Test that access log fields %BYTES_RECEIVED%, %BYTES_SENT%, %START_TIME%, %DURATION% are
// all correctly logged.
TEST_F(TcpProxyTest, AccessLogBytesRxTxDuration) {
  setup(true, R"EOF(
      {
        "path": "unused",
        "format": "bytesreceived=%BYTES_RECEIVED% bytessent=%BYTES_SENT% datetime=%START_TIME% nonzeronum=%DURATION%"
      }
    )EOF");

  upstream_connection_->raiseEvent(Network::ConnectionEvent::Connected);
  Buffer::OwnedImpl buffer("a");
  filter_->onData(buffer);
  Buffer::OwnedImpl response("bb");
  upstream_read_filter_->onData(response);

  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();

  EXPECT_THAT(access_log_data_,
              MatchesRegex(
                  "bytesreceived=1 bytessent=2 datetime=[0-9-]+T[0-9:.]+Z nonzeronum=[1-9][0-9]*"));
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
    config_.reset(new TcpProxyConfig(constructTcpProxyConfigFromJson(*config, factory_context_)));
  }

  void setup() {
    EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

    filter_.reset(new TcpProxy(config_, factory_context_.cluster_manager_));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  TcpProxyConfigSharedPtr config_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
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
  EXPECT_CALL(factory_context_.cluster_manager_, tcpConnForCluster_("fake_cluster", _));

  filter_->onNewConnection();

  EXPECT_EQ(total_cx + 1, config_->stats().downstream_cx_total_.value());
  EXPECT_EQ(non_routable_cx, config_->stats().downstream_cx_no_route_.value());
}

} // namespace Filter
} // namespace Envoy
