#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.validate.h"

#include "test/integration/http_integration.h"
#include "test/integration/integration.h"

using envoy::config::core::v3::ProxyProtocolPassThroughTLVs;
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
             bool inner_tls) {
    version_ = version;
    health_checks_ = health_checks;
    inner_tls_ = inner_tls;
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.upstream_proxy_protocol");
      envoy::config::core::v3::TransportSocket inner_socket;
      if (inner_tls_) {
        envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls;
        inner_socket.set_name("tls");
        inner_socket.mutable_typed_config()->PackFrom(tls);
      } else {
        envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
        inner_socket.set_name("raw");
        inner_socket.mutable_typed_config()->PackFrom(raw_buffer);
      }
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
  bool inner_tls_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolTcpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test sending proxy protocol v1
TEST_P(ProxyProtocolTcpIntegrationTest, TestV1ProxyProtocol) {
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false, false);
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

  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false, false);
  initialize();
  auto listener_port = lookupPort("listener_0");

  auto loopback2 = *Network::Utility::resolveUrl("tcp://127.0.0.2:0");
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
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false, true);
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
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, true, false);
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
  setup(envoy::config::core::v3::ProxyProtocolConfig::V2, false, false);
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
    const char data[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
                         0x21, 0x11, 0x00, 0x0c, 0x7f, 0x00, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x01};
    absl::string_view header_start(data, sizeof(data));
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));
    EXPECT_EQ(static_cast<uint8_t>(observed_data[26]), listener_port >> 8);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[27]), listener_port & 0xFF);
  } else if (GetParam() == Envoy::Network::Address::IpVersion::v6) {
    ASSERT_TRUE(fake_upstream_connection_->waitForData(56, &observed_data));
    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address
    // - dest address
    const char data[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
                         0x21, 0x21, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    absl::string_view header_start(data, sizeof(data));
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

  void setup(envoy::config::core::v3::ProxyProtocolConfig_Version version, bool health_checks) {
    version_ = version;
    health_checks_ = health_checks;
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.upstream_proxy_protocol");
      envoy::config::core::v3::TransportSocket inner_socket;
      envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
      inner_socket.set_name("raw");
      inner_socket.mutable_typed_config()->PackFrom(raw_buffer);
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
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test sending proxy protocol over http
TEST_P(ProxyProtocolHttpIntegrationTest, TestV1ProxyProtocol) {
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false);
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

  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, false);
  initialize();
  auto listener_port = lookupPort("http");
  auto tcp_client = makeTcpConnection(listener_port);
  auto loopback2 = *Network::Utility::resolveUrl("tcp://127.0.0.2:0");
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
  setup(envoy::config::core::v3::ProxyProtocolConfig::V1, true);
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

class ProxyProtocolTLVsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public BaseIntegrationTest {
public:
  ProxyProtocolTLVsIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()){};

  void TearDown() override {
    test_server_.reset();
    fake_upstream_connection_.reset();
    fake_upstreams_.clear();
  }

  void setup(bool pass_all_tlvs, const std::vector<uint8_t>& tlvs_listener,
             const std::vector<uint8_t>& tlvs_upstream) {
    pass_all_tlvs_ = pass_all_tlvs;
    tlvs_listener_.assign(tlvs_listener.begin(), tlvs_listener.end());
    tlvs_upstream_.assign(tlvs_upstream.begin(), tlvs_upstream.end());
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proxy_protocol;
      auto pass_through_tlvs = proxy_protocol.mutable_pass_through_tlvs();
      if (pass_all_tlvs_) {
        pass_through_tlvs->set_match_type(ProxyProtocolPassThroughTLVs::INCLUDE_ALL);
      } else {
        pass_through_tlvs->set_match_type(ProxyProtocolPassThroughTLVs::INCLUDE);
        for (const auto& tlv_type : tlvs_listener_) {
          pass_through_tlvs->add_tlv_type(tlv_type);
        }
      }

      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* ppv_filter = listener->add_listener_filters();
      ppv_filter->set_name("envoy.listener.proxy_protocol");
      ppv_filter->mutable_typed_config()->PackFrom(proxy_protocol);
    });

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.upstream_proxy_protocol");
      envoy::config::core::v3::TransportSocket inner_socket;
      envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
      inner_socket.set_name("raw");
      inner_socket.mutable_typed_config()->PackFrom(raw_buffer);

      envoy::config::core::v3::ProxyProtocolConfig proxy_protocol;
      proxy_protocol.set_version(envoy::config::core::v3::ProxyProtocolConfig::V2);
      auto pass_through_tlvs = proxy_protocol.mutable_pass_through_tlvs();

      if (pass_all_tlvs_) {
        pass_through_tlvs->set_match_type(ProxyProtocolPassThroughTLVs::INCLUDE_ALL);
      } else {
        pass_through_tlvs->set_match_type(ProxyProtocolPassThroughTLVs::INCLUDE);
        for (const auto& tlv_type : tlvs_upstream_) {
          pass_through_tlvs->add_tlv_type(tlv_type);
        }
      }

      envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocolUpstreamTransport
          proxy_proto_transport;
      proxy_proto_transport.mutable_transport_socket()->MergeFrom(inner_socket);
      proxy_proto_transport.mutable_config()->MergeFrom(proxy_protocol);
      transport_socket->mutable_typed_config()->PackFrom(proxy_proto_transport);
    });

    BaseIntegrationTest::initialize();
  }

  FakeRawConnectionPtr fake_upstream_connection_;

private:
  bool pass_all_tlvs_ = false;
  std::vector<uint8_t> tlvs_listener_;
  std::vector<uint8_t> tlvs_upstream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolTLVsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// This test adding the listener proxy protocol filter and upstream proxy filter, the TLVs
// are passed by listener and re-generated in transport socket based on API config.
TEST_P(ProxyProtocolTLVsIntegrationTest, TestV2TLVProxyProtocolPassSepcificTLVs) {
  setup(false, {0x05, 0x06}, {0x06});
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  std::string observed_data;
  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    // 2 TLVs are included:
    // 0x05, 0x00, 0x02, 0x06, 0x07
    // 0x06, 0x00, 0x02, 0x11, 0x12
    const uint8_t v2_protocol[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                   0x54, 0x0a, 0x21, 0x11, 0x00, 0x16, 0x7f, 0x00, 0x00, 0x01,
                                   0x7f, 0x00, 0x00, 0x01, 0x03, 0x05, 0x02, 0x01, 0x05, 0x00,
                                   0x02, 0x06, 0x07, 0x06, 0x00, 0x02, 0x11, 0x12};
    Buffer::OwnedImpl buffer(v2_protocol, sizeof(v2_protocol));
    ASSERT_TRUE(tcp_client->write(buffer.toString()));
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForData(33, &observed_data));

    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address, dest address
    const char data[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
                         0x21, 0x11, 0x00, 0x11, 0x7f, 0x00, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x01};
    absl::string_view header_start(data, sizeof(data));
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));

    // Only tlv: 0x06, 0x00, 0x02, 0x11, 0x12 is sent to upstream.
    EXPECT_EQ(static_cast<uint8_t>(observed_data[28]), 0x06);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[29]), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[30]), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[31]), 0x11);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[32]), 0x12);
  } else if (GetParam() == Envoy::Network::Address::IpVersion::v6) {
    // 2 TLVs are included:
    // 0x05, 0x00, 0x02, 0x06, 0x07
    // 0x06, 0x00, 0x02, 0x09, 0x0A
    const uint8_t v2_protocol_ipv6[] = {
        0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21,
        0x21, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x02,
        0x05, 0x00, 0x02, 0x06, 0x07, 0x06, 0x00, 0x02, 0x09, 0x0A};
    Buffer::OwnedImpl buffer(v2_protocol_ipv6, sizeof(v2_protocol_ipv6));
    ASSERT_TRUE(tcp_client->write(buffer.toString()));
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

    ASSERT_TRUE(fake_upstream_connection_->waitForData(57, &observed_data));
    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address
    // - dest address
    const char data[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
                         0x21, 0x21, 0x00, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    absl::string_view header_start(data, sizeof(data));
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));

    // Only tlv: 0x06, 0x00, 0x02, 0x09, 0x0A is sent to upstream.
    EXPECT_EQ(static_cast<uint8_t>(observed_data[52]), 0x06);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[53]), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[54]), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[55]), 0x09);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[56]), 0x0A);
  }

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(ProxyProtocolTLVsIntegrationTest, TestV2TLVProxyProtocolPassAll) {
  setup(true, {}, {});
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  std::string observed_data;
  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    // 2 TLVs are included:
    // 0x05, 0x00, 0x02, 0x06, 0x07
    // 0x06, 0x00, 0x02, 0x11, 0x12
    const uint8_t v2_protocol[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                   0x54, 0x0a, 0x21, 0x11, 0x00, 0x16, 0x7f, 0x00, 0x00, 0x01,
                                   0x7f, 0x00, 0x00, 0x01, 0x03, 0x05, 0x02, 0x01, 0x05, 0x00,
                                   0x02, 0x06, 0x07, 0x06, 0x00, 0x02, 0x11, 0x12};
    Buffer::OwnedImpl buffer(v2_protocol, sizeof(v2_protocol));
    ASSERT_TRUE(tcp_client->write(buffer.toString()));
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForData(38, &observed_data));

    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address, dest address
    const char data[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
                         0x21, 0x11, 0x00, 0x16, 0x7f, 0x00, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x01};
    absl::string_view header_start(data, sizeof(data));
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));

    // Only tlv: 0x06, 0x00, 0x02, 0x11, 0x12 is sent to upstream.
    EXPECT_EQ(static_cast<uint8_t>(observed_data[28]), 0x05);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[29]), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[30]), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[31]), 0x06);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[32]), 0x07);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[33]), 0x06);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[34]), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[35]), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[36]), 0x11);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[37]), 0x12);
  } else if (GetParam() == Envoy::Network::Address::IpVersion::v6) {
    // 2 TLVs are included:
    // 0x05, 0x00, 0x02, 0x06, 0x07
    // 0x06, 0x00, 0x02, 0x09, 0x0A
    const uint8_t v2_protocol_ipv6[] = {
        0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a, 0x21,
        0x21, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x02,
        0x05, 0x00, 0x02, 0x06, 0x07, 0x06, 0x00, 0x02, 0x09, 0x0A};
    Buffer::OwnedImpl buffer(v2_protocol_ipv6, sizeof(v2_protocol_ipv6));
    ASSERT_TRUE(tcp_client->write(buffer.toString()));
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));

    ASSERT_TRUE(fake_upstream_connection_->waitForData(62, &observed_data));
    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address
    // - dest address
    const char data[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
                         0x21, 0x21, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    absl::string_view header_start(data, sizeof(data));
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));

    // Only tlv: 0x06, 0x00, 0x02, 0x09, 0x0A is sent to upstream.
    EXPECT_EQ(static_cast<uint8_t>(observed_data[52]), 0x05);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[53]), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[54]), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[55]), 0x06);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[56]), 0x07);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[57]), 0x06);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[58]), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[59]), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[60]), 0x09);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[61]), 0x0A);
  }

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(ProxyProtocolTLVsIntegrationTest, TestV2ProxyProtocolPassWithTypeLocal) {
  setup(true, {}, {});
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));

  // A well-formed proxy protocol v2 header sampled from an AWS NLB healthcheck request, with
  // command type 'LOCAL' (0 for the low 4 bits of the 13th octet).
  constexpr uint8_t v2_protocol[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51,
                                     0x55, 0x49, 0x54, 0x0a, 0x20, 0x00, 0x00, 0x00,
                                     'm',  'o',  'r',  'e',  'd',  'a',  't',  'a'};
  Buffer::OwnedImpl buffer(v2_protocol, sizeof(v2_protocol));
  ASSERT_TRUE(tcp_client->write(buffer.toString()));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_));
  std::string header_start;
  // - signature
  // - version and command type, address family and protocol, length of addresses
  // - src address, dest address
  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    const char data[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
                         0x21, 0x11, 0x00, 0x0c, 0x7f, 0x00, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x01};
    header_start = std::string(data, sizeof(data));
  } else {
    const char data[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a,
                         0x21, 0x21, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    header_start = std::string(data, sizeof(data));
  }

  constexpr absl::string_view more_data("moredata");
  const size_t offset = header_start.length() + (2 * sizeof(uint16_t)); // Skip over the ports
  std::string observed_data;
  ASSERT_TRUE(fake_upstream_connection_->waitForData(offset + more_data.length(), &observed_data));
  EXPECT_THAT(observed_data, testing::StartsWith(header_start));
  EXPECT_EQ(more_data, absl::string_view(&observed_data[offset], more_data.length()));

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

} // namespace
} // namespace Envoy
