#include "test/integration/proxy_proto_integration_test.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"

#include "common/buffer/buffer_impl.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "fmt/format.h"
#include "gtest/gtest.h"

namespace Envoy {

static void
insertProxyProtocolFilterConfigModifier(envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  ::envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol proxy_protocol;
  auto rule = proxy_protocol.add_rules();
  rule->set_tlv_type(0x02);
  rule->mutable_on_tlv_present()->set_key("PP2TypeAuthority");

  auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
  auto* ppv_filter = listener->add_listener_filters();
  ppv_filter->set_name("envoy.listener.proxy_protocol");
  ppv_filter->mutable_typed_config()->PackFrom(proxy_protocol);
}

ProxyProtoIntegrationTest::ProxyProtoIntegrationTest()
    : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {
  config_helper_.addConfigModifier(insertProxyProtocolFilterConfigModifier);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtoIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ProxyProtoIntegrationTest, CaptureTlvToMetadata) {
  useListenerAccessLog(
      "%DYNAMIC_METADATA(envoy.filters.listener.proxy_protocol:PP2TypeAuthority)%");

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                  0x0a, 0x21, 0x11, 0x00, 0x1a, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01,
                                  0x01, 0x02, 0x03, 0x05, 0x00, 0x02, 0x00, 0x00, 0x01, 0xff, 0x02,
                                  0x00, 0x07, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d};
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  cleanupUpstreamAndDownstream();
  const std::string log_line = waitForAccessLog(listener_access_log_name_);
  EXPECT_EQ(log_line, "\"foo.com\"");
}

TEST_P(ProxyProtoIntegrationTest, V1RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP4 1.2.3.4 254.254.254.254 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, V2RouterRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49,
                                  0x54, 0x0a, 0x21, 0x11, 0x00, 0x0c, 0x01, 0x02, 0x03, 0x04,
                                  0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xfe, 0x04, 0xd2};
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, V1RouterRequestAndResponseWithBodyNoBufferV6) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP6 1:2:3::4 FE00:: 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, V2RouterRequestAndResponseWithBodyNoBufferV6) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    constexpr uint8_t buffer[] = {0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51, 0x55, 0x49, 0x54,
                                  0x0a, 0x21, 0x22, 0x00, 0x24, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
                                  0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02};
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf(buffer, sizeof(buffer));
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, RouterProxyUnknownRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY UNKNOWN\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, RouterProxyUnknownLongRequestAndResponseWithBodyNoBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    auto conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY UNKNOWN 1:2:3::4 FE00:: 65535 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// Test that %DOWNSTREAM_DIRECT_REMOTE_ADDRESS%/%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT%
// returns the direct address, and %DOWSTREAM_REMOTE_ADDRESS% returns the proxy-protocol-provided
// address.
TEST_P(ProxyProtoIntegrationTest, AccessLog) {
  useAccessLog("%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT% %DOWNSTREAM_REMOTE_ADDRESS%");

  // Tell HCM to ignore x-forwarded-for so that the proxy-proto address is used.
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_use_remote_address()->set_value(true); });

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    Buffer::OwnedImpl buf("PROXY TCP4 1.2.3.4 254.254.254.254 12345 1234\r\n");
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  const std::string log_line = waitForAccessLog(access_log_name_);
  const std::vector<absl::string_view> tokens = StringUtil::splitToken(log_line, " ");

  ASSERT_EQ(2, tokens.size());
  EXPECT_EQ(tokens[0], Network::Test::getLoopbackAddressString(GetParam()));
  EXPECT_EQ(tokens[1], "1.2.3.4:12345\n");
}

TEST_P(ProxyProtoIntegrationTest, DEPRECATED_FEATURE_TEST(OriginalDst)) {
  // Change the cluster to an original destination cluster. An original destination cluster
  // ignores the configured hosts, and instead uses the restored destination address from the
  // incoming (server) connection as the destination address for the outgoing (client) connection.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->clear_load_assignment();
    cluster->set_type(envoy::config::cluster::v3::Cluster::ORIGINAL_DST);
    cluster->set_lb_policy(
        envoy::config::cluster::v3::Cluster::hidden_envoy_deprecated_ORIGINAL_DST_LB);
  });

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    // Create proxy protocol line that has the fake upstream address as the destination address.
    // This address will become the "restored" address for the server connection and will
    // be used as the destination address by the original destination cluster.
    std::string proxyLine = fmt::format(
        "PROXY {} {} 65535 {}\r\n",
        GetParam() == Network::Address::IpVersion::v4 ? "TCP4 1.2.3.4" : "TCP6 1:2:3::4",
        Network::Test::getLoopbackAddressString(GetParam()),
        fake_upstreams_[0]->localAddress()->ip()->port());

    Buffer::OwnedImpl buf(proxyLine);
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(ProxyProtoIntegrationTest, ClusterProvided) {
  // Change the cluster to an original destination cluster. An original destination cluster
  // ignores the configured hosts, and instead uses the restored destination address from the
  // incoming (server) connection as the destination address for the outgoing (client) connection.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->clear_load_assignment();
    cluster->set_type(envoy::config::cluster::v3::Cluster::ORIGINAL_DST);
    cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
  });

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    Network::ClientConnectionPtr conn = makeClientConnection(lookupPort("http"));
    // Create proxy protocol line that has the fake upstream address as the destination address.
    // This address will become the "restored" address for the server connection and will
    // be used as the destination address by the original destination cluster.
    std::string proxyLine = fmt::format(
        "PROXY {} {} 65535 {}\r\n",
        GetParam() == Network::Address::IpVersion::v4 ? "TCP4 1.2.3.4" : "TCP6 1:2:3::4",
        Network::Test::getLoopbackAddressString(GetParam()),
        fake_upstreams_[0]->localAddress()->ip()->port());

    Buffer::OwnedImpl buf(proxyLine);
    conn->write(buf, false);
    return conn;
  };

  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

ProxyProtoTcpIntegrationTest::ProxyProtoTcpIntegrationTest()
    : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
  config_helper_.addConfigModifier(insertProxyProtocolFilterConfigModifier);
  config_helper_.renameListener("tcp_proxy");
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtoTcpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// This tests that the StreamInfo contains the correct addresses.
TEST_P(ProxyProtoTcpIntegrationTest, AccessLog) {
  std::string access_log_path = TestEnvironment::temporaryPath(
      fmt::format("access_log{}.txt", version_ == Network::Address::IpVersion::v4 ? "v4" : "v6"));
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(
        config_blob->Is<API_NO_BOOST(envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>());
    auto tcp_proxy_config = MessageUtil::anyConvert<API_NO_BOOST(
        envoy::config::filter::network::tcp_proxy::v2::TcpProxy)>(*config_blob);

    auto* access_log = tcp_proxy_config.add_access_log();
    access_log->set_name("accesslog");
    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.set_path(access_log_path);
    access_log_config.mutable_log_format()->set_text_format(
        "remote=%DOWNSTREAM_REMOTE_ADDRESS% local=%DOWNSTREAM_LOCAL_ADDRESS%");
    access_log->mutable_typed_config()->PackFrom(access_log_config);
    config_blob->PackFrom(tcp_proxy_config);
  });
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("PROXY TCP4 1.2.3.4 254.254.254.254 12345 1234\r\nhello", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  std::string log_result;
  // Access logs only get flushed to disk periodically, so poll until the log is non-empty
  do {
    log_result = api_->fileSystem().fileReadToEnd(access_log_path);
  } while (log_result.empty());

  EXPECT_EQ(log_result, "remote=1.2.3.4:12345 local=254.254.254.254:1234");
}

} // namespace Envoy
