#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/integration.h"

namespace Envoy {
namespace {

class ProxyProtocolTcpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public BaseIntegrationTest {
public:
  ProxyProtocolTcpIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void TearDown() override {
    test_server_.reset();
    fake_upstream_connection_.reset();
    fake_upstreams_.clear();
  }

  void setup(envoy::config::core::v3::ProxyProtocolConfig_Version version, bool health_checks,
             std::string inner_socket) {
    version_ = version;
    health_checks_ = health_checks;
    inner_socket_ = inner_socket;
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.upstream_proxy_protocol");
      envoy::config::core::v3::TransportSocket inner_socket;
      inner_socket.set_name(inner_socket_);
      envoy::config::core::v3::ProxyProtocolConfig proxy_proto_config;
      proxy_proto_config.set_version(version_);
      envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocolUpstreamTransport
          proxy_proto_transport;
      proxy_proto_transport.mutable_transport_socket()->MergeFrom(inner_socket);
      proxy_proto_transport.mutable_config()->MergeFrom(proxy_proto_config);
      transport_socket->mutable_typed_config()->PackFrom(proxy_proto_transport);

      if (health_checks_) {
        auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
        cluster->set_close_connections_on_host_health_failure(false);
        cluster->mutable_common_lb_config()->mutable_healthy_panic_threshold()->set_value(0);
        cluster->add_health_checks()->mutable_timeout()->set_seconds(20);
        cluster->mutable_health_checks(0)->mutable_reuse_connection()->set_value(true);
        cluster->mutable_health_checks(0)->mutable_interval()->set_seconds(1);
        cluster->mutable_health_checks(0)->mutable_no_traffic_interval()->set_seconds(1);
        cluster->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(1);
        cluster->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(1);
        cluster->mutable_health_checks(0)->mutable_tcp_health_check();
        cluster->mutable_health_checks(0)->mutable_tcp_health_check()->mutable_send()->set_text(
            "50696E67");
        cluster->mutable_health_checks(0)->mutable_tcp_health_check()->add_receive()->set_text(
            "506F6E67");
      }
    });
    BaseIntegrationTest::initialize();
  }

  FakeRawConnectionPtr fake_upstream_connection_;

private:
  envoy::config::core::v3::ProxyProtocolConfig_Version version_;
  bool health_checks_;
  std::string inner_socket_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolTcpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test sending proxy protocol v1
TEST_P(ProxyProtocolTcpIntegrationTest, TestV1ProxyProtocol) {
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false,
        "envoy.transport_sockets.raw_buffer");
  initialize();

  auto listener_port = lookupPort("listener_0");
  auto tcp_client = makeTcpConnection(listener_port);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  std::string observed_data;
  ASSERT_TRUE(tcp_client->write("data"));
  if (GetParam() == Network::Address::IpVersion::v4) {
    ASSERT_TRUE(fake_upstream_connection_->waitForData(48, &observed_data));
    EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP4 127.0.0.1 127.0.0.1 "));
  } else if (GetParam() == Network::Address::IpVersion::v6) {
    ASSERT_TRUE(fake_upstream_connection_->waitForData(36, &observed_data));
    EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP6 ::1 ::1 "));
  }
  EXPECT_THAT(observed_data, testing::EndsWith(absl::StrCat(" ", listener_port, "\r\ndata")));

  auto previous_data = observed_data;
  observed_data.clear();
  ASSERT_TRUE(tcp_client->write(" more data"));
  ASSERT_TRUE(fake_upstream_connection_->waitForData(previous_data.length() + 10, &observed_data));
  EXPECT_EQ(previous_data + " more data", observed_data);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(ProxyProtocolTcpIntegrationTest, TestV1ProxyProtocolMultipleConnections) {
  if (GetParam() != Network::Address::IpVersion::v4) {
    return;
  }

  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false,
        "envoy.transport_sockets.raw_buffer");
  initialize();
  auto listener_port = lookupPort("listener_0");

  auto loopback2 = Network::Utility::resolveUrl("tcp://127.0.0.2:0");
  auto tcp_client2 = makeTcpConnection(listener_port, nullptr, loopback2);

  auto tcp_client = makeTcpConnection(listener_port);

  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));
  FakeRawConnectionPtr conn2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn2));

  std::string data1, data2;
  ASSERT_TRUE(
      fake_upstream_connection_->waitForData(FakeRawConnection::waitForAtLeastBytes(32), &data1));
  ASSERT_TRUE(conn2->waitForData(FakeRawConnection::waitForAtLeastBytes(32), &data2));

  EXPECT_NE(data1, data2);

  tcp_client->close();
  tcp_client2->close();
}

// Test header is sent unencrypted using a TLS inner socket
TEST_P(ProxyProtocolTcpIntegrationTest, TestTLSSocket) {
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false, "envoy.transport_sockets.tls");
  initialize();

  auto listener_port = lookupPort("listener_0");
  auto tcp_client = makeTcpConnection(listener_port);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  ASSERT_TRUE(tcp_client->write("data"));
  if (GetParam() == Network::Address::IpVersion::v4) {
    ASSERT_TRUE(fake_upstream_connection_->waitForData(
        fake_upstream_connection_->waitForInexactMatch("PROXY TCP4 127.0.0.1 127.0.0.1 ")));
  } else if (GetParam() == Network::Address::IpVersion::v6) {
    ASSERT_TRUE(fake_upstream_connection_->waitForData(
        fake_upstream_connection_->waitForInexactMatch("PROXY TCP6 ::1 ::1 ")));
  }

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

// Test sending proxy protocol health check
TEST_P(ProxyProtocolTcpIntegrationTest, TestProxyProtocolHealthCheck) {
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, true,
        "envoy.transport_sockets.raw_buffer");
  FakeRawConnectionPtr fake_upstream_health_connection;
  on_server_init_function_ = [&](void) -> void {
    std::string observed_data;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_health_connection));
    if (GetParam() == Network::Address::IpVersion::v4) {
      ASSERT_TRUE(fake_upstream_health_connection->waitForData(48, &observed_data));
      EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP4 127.0.0.1 127.0.0.1 "));
    } else if (GetParam() == Network::Address::IpVersion::v6) {
      ASSERT_TRUE(fake_upstream_health_connection->waitForData(36, &observed_data));
      EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP6 ::1 ::1 "));
    }
    ASSERT_TRUE(fake_upstream_health_connection->write("Pong"));
  };

  initialize();

  ASSERT_TRUE(fake_upstream_health_connection->close());
  ASSERT_TRUE(fake_upstream_health_connection->waitForDisconnect());
}

// Test sending proxy protocol v2
TEST_P(ProxyProtocolTcpIntegrationTest, TestV2ProxyProtocol) {
  setup(envoy::config::core::v3::ProxyProtocolConfig::V2, false,
        "envoy.transport_sockets.raw_buffer");
  initialize();

  auto listener_port = lookupPort("listener_0");
  auto tcp_client = makeTcpConnection(listener_port);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

  std::string observed_data;
  ASSERT_TRUE(tcp_client->write("data"));
  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    ASSERT_TRUE(fake_upstream_connection_->waitForData(32, &observed_data));
    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address, dest address
    auto header_start = "\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a\
                         \x21\x11\x00\x0c\
                         \x7f\x00\x00\x01\x7f\x00\x00\x01";
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));
    EXPECT_EQ(static_cast<uint8_t>(observed_data[26]), listener_port >> 8);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[27]), listener_port & 0xFF);
  } else if (GetParam() == Envoy::Network::Address::IpVersion::v6) {
    ASSERT_TRUE(fake_upstream_connection_->waitForData(56, &observed_data));
    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address
    // - dest address
    auto header_start = "\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a\
                         \x21\x21\x00\x24\
                         \x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\
                         \x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01";
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));
    EXPECT_EQ(static_cast<uint8_t>(observed_data[50]), listener_port >> 8);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[51]), listener_port & 0xFF);
  }
  EXPECT_THAT(observed_data, testing::EndsWith("data"));

  auto previous_data = observed_data;
  observed_data.clear();
  ASSERT_TRUE(tcp_client->write(" more data"));
  ASSERT_TRUE(fake_upstream_connection_->waitForData(previous_data.length() + 10, &observed_data));
  EXPECT_EQ(previous_data + " more data", observed_data);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

class ProxyProtocolHttpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public HttpIntegrationTest {
public:
  ProxyProtocolHttpIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void TearDown() override {
    test_server_.reset();
    fake_upstream_connection_.reset();
    fake_upstreams_.clear();
  }

  void setup(envoy::config::core::v3::ProxyProtocolConfig_Version version, bool health_checks,
             std::string inner_socket) {
    version_ = version;
    health_checks_ = health_checks;
    inner_socket_ = inner_socket;
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.upstream_proxy_protocol");
      envoy::config::core::v3::TransportSocket inner_socket;
      inner_socket.set_name(inner_socket_);
      envoy::config::core::v3::ProxyProtocolConfig proxy_proto_config;
      proxy_proto_config.set_version(version_);
      envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocolUpstreamTransport
          proxy_proto_transport;
      proxy_proto_transport.mutable_transport_socket()->MergeFrom(inner_socket);
      proxy_proto_transport.mutable_config()->MergeFrom(proxy_proto_config);
      transport_socket->mutable_typed_config()->PackFrom(proxy_proto_transport);

      if (health_checks_) {
        auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
        cluster->set_close_connections_on_host_health_failure(false);
        cluster->mutable_common_lb_config()->mutable_healthy_panic_threshold()->set_value(0);
        cluster->add_health_checks()->mutable_timeout()->set_seconds(20);
        cluster->mutable_health_checks(0)->mutable_reuse_connection()->set_value(true);
        cluster->mutable_health_checks(0)->mutable_interval()->set_seconds(1);
        cluster->mutable_health_checks(0)->mutable_no_traffic_interval()->set_seconds(1);
        cluster->mutable_health_checks(0)->mutable_unhealthy_threshold()->set_value(1);
        cluster->mutable_health_checks(0)->mutable_healthy_threshold()->set_value(1);
        cluster->mutable_health_checks(0)->mutable_http_health_check()->set_codec_client_type(
            envoy::type::v3::HTTP1);
        cluster->mutable_health_checks(0)->mutable_http_health_check()->set_path("/healthcheck");
      }
    });
    BaseIntegrationTest::initialize();
  }

private:
  envoy::config::core::v3::ProxyProtocolConfig_Version version_;
  bool health_checks_;
  std::string inner_socket_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test sending proxy protocol over http
TEST_P(ProxyProtocolHttpIntegrationTest, TestV1ProxyProtocol) {
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false,
        "envoy.transport_sockets.raw_buffer");
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("http"));
  auto request = "GET / HTTP/1.1\r\nhost: host\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(request, false));
  FakeRawConnectionPtr fake_upstream_raw_connection_;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_raw_connection_));

  std::string observed_data;
  if (GetParam() == Network::Address::IpVersion::v4) {
    ASSERT_TRUE(fake_upstream_raw_connection_->waitForData(
        FakeRawConnection::waitForAtLeastBytes(70), &observed_data));
    EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP4 127.0.0.1 127.0.0.1 "));
  } else if (GetParam() == Network::Address::IpVersion::v6) {
    ASSERT_TRUE(fake_upstream_raw_connection_->waitForData(
        FakeRawConnection::waitForAtLeastBytes(58), &observed_data));
    EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP6 ::1 ::1 "));
  }
  EXPECT_TRUE(absl::StrContains(observed_data, "GET / HTTP/1.1"));
  EXPECT_TRUE(absl::StrContains(observed_data, "host: host"));

  auto response = "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n";
  ASSERT_TRUE(fake_upstream_raw_connection_->write(response, false));
  tcp_client->waitForData("HTTP/1.1 200 OK\r\ncontent-length: 0", true);

  std::string delimiter = "\r\n";
  std::string after_proxy_proto_header = observed_data.substr(
      observed_data.find(delimiter) + delimiter.length(), observed_data.length());

  auto previous_data = observed_data;
  observed_data.clear();
  ASSERT_TRUE(tcp_client->write(request, false));
  ASSERT_TRUE(fake_upstream_raw_connection_->waitForData(
      previous_data.length() + after_proxy_proto_header.length(), &observed_data));
  ASSERT_TRUE(observed_data.length() == previous_data.length() + after_proxy_proto_header.length());
  tcp_client->close();
}

// Test sending proxy protocol over multiple http connections
TEST_P(ProxyProtocolHttpIntegrationTest, TestV1ProxyProtocolMultipleConnections) {
  if (GetParam() != Network::Address::IpVersion::v4) {
    return;
  }

  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false,
        "envoy.transport_sockets.raw_buffer");
  initialize();
  auto listener_port = lookupPort("http");
  auto tcp_client = makeTcpConnection(listener_port);
  auto loopback2 = Network::Utility::resolveUrl("tcp://127.0.0.2:0");
  auto tcp_client2 = makeTcpConnection(listener_port, nullptr, loopback2);

  auto request = "GET / HTTP/1.1\r\nhost: host\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(request, false));
  ASSERT_TRUE(tcp_client2->write(request, false));

  FakeRawConnectionPtr conn1, conn2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn1));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn2));
  std::string data1, data2;
  ASSERT_TRUE(conn1->waitForData(FakeRawConnection::waitForAtLeastBytes(48), &data1));
  ASSERT_TRUE(conn2->waitForData(FakeRawConnection::waitForAtLeastBytes(48), &data2));

  std::string delimiter = "\r\n";
  std::string conn1_header = data1.substr(0, data1.find(delimiter));
  std::string conn2_header = data2.substr(0, data2.find(delimiter));

  EXPECT_NE(conn1_header, conn2_header);

  tcp_client->close();
  tcp_client2->close();
}

// Test sending proxy protocol http health check
TEST_P(ProxyProtocolHttpIntegrationTest, TestProxyProtocolHealthCheck) {
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, true,
        "envoy.transport_sockets.raw_buffer");
  FakeRawConnectionPtr fake_upstream_health_connection;
  on_server_init_function_ = [&](void) -> void {
    std::string observed_data;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_health_connection));
    if (GetParam() == Network::Address::IpVersion::v4) {
      ASSERT_TRUE(fake_upstream_health_connection->waitForData(
          FakeRawConnection::waitForAtLeastBytes(48), &observed_data));
      EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP4 127.0.0.1 127.0.0.1 "));
    } else if (GetParam() == Network::Address::IpVersion::v6) {
      ASSERT_TRUE(fake_upstream_health_connection->waitForData(
          FakeRawConnection::waitForAtLeastBytes(36), &observed_data));
      EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP6 ::1 ::1 "));
    }
    auto response = "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n";
    ASSERT_TRUE(fake_upstream_health_connection->write(response));
  };

  initialize();

  ASSERT_TRUE(fake_upstream_health_connection->close());
  ASSERT_TRUE(fake_upstream_health_connection->waitForDisconnect());
}

} // namespace
} // namespace Envoy
