#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/http/headers.h"
#include "source/common/network/utility.h"

#include "test/common/upstream/utility.h"
#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

using OriginalDstIntegrationTest = HttpProtocolIntegrationTest;

ConfigHelper::ConfigModifierFunction setOriginalDstCluster(int port) {
  return [port](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.clear_load_balancing_policy();

    cluster.set_type(envoy::config::cluster::v3::Cluster::ORIGINAL_DST);
    cluster.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
    cluster.mutable_original_dst_lb_config()->mutable_upstream_port_override()->set_value(port);
    cluster.clear_load_assignment();

    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->mutable_address()->mutable_socket_address()->set_address("0.0.0.0");
    listener->set_traffic_direction(envoy::config::core::v3::INBOUND);

    auto* listener_filter = listener->add_listener_filters();
    listener_filter->set_name("envoy.filters.listener.original_dst");
    envoy::extensions::filters::listener::original_dst::v3::OriginalDst original_dst;
    std::ignore = listener_filter->mutable_typed_config()->PackFrom(original_dst);
  };
}

INSTANTIATE_TEST_SUITE_P(Protocols, OriginalDstIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(OriginalDstIntegrationTest, OriginalDstHttpManyConnections) {
  // Windows apparently have this loopback property.
  DISABLE_UNDER_WINDOWS;
  // Only do this for IPv4 as we can have many 127.0.0.x addresses listening on 0.0.0.0
  if (version_ != Network::Address::IpVersion::v4) {
    return;
  }
  ASSERT(!upstream_tls_);
  auto address = Network::Test::getAnyAddress(Network::Address::IpVersion::v4);
  fake_upstreams_.emplace_back(
      std::make_unique<FakeUpstream>(Network::Test::createRawBufferDownstreamSocketFactory(),
                                     address, configWithType(upstreamProtocol())));

  config_helper_.addConfigModifier(
      setOriginalDstCluster(fake_upstreams_[0]->localAddress()->ip()->port()));
  initialize();

  const int32_t kMaxConnections = 10;
  for (int i = 0; i < kMaxConnections; ++i) {
    std::string address_str = fmt::format("127.0.0.{}", i + 1);
    Network::ClientConnectionPtr connection(dispatcher_->createClientConnection(
        *Network::Utility::resolveUrl(fmt::format("tcp://{}:{}", address_str, lookupPort("http"))),
        Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr,
        nullptr));

    connection->enableHalfClose(enableHalfClose());

    codec_client_ = makeHttpConnection(std::move(connection));
    auto response =
        sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
    EXPECT_EQ(address_str, fake_upstream_connection_->connection()
                               .connectionInfoProvider()
                               .localAddress()
                               ->ip()
                               ->addressAsString());
    ASSERT_TRUE(fake_upstream_connection_->close());
    fake_upstream_connection_.reset();
    codec_client_->close();
  }
}

TEST_P(OriginalDstIntegrationTest, ScopedIpv6OriginalDstCrash) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.clear_load_balancing_policy();

    cluster.set_type(envoy::config::cluster::v3::Cluster::ORIGINAL_DST);
    cluster.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
    cluster.mutable_original_dst_lb_config()->set_use_http_header(true);
    cluster.clear_load_assignment();
  });

  initialize();

  // Send a raw HTTP/1.1 request with a scoped link-local IPv6 in the override header.
  auto tcp = makeTcpConnection(lookupPort("http"));
  ASSERT_TRUE(tcp->write("GET / HTTP/1.1\r\n"
                         "Host: h\r\n"
                         "x-envoy-original-dst-host: [fe80::1%1]:80\r\n"
                         "\r\n",
                         false));

  // Since chooseHost() now successfully handles the scoped IPv6 address without throwing/aborting,
  // Envoy should gracefully handle the request and return a 503 response because the target is
  // unreachable.
  tcp->waitForData("HTTP/1.1 503");
  EXPECT_TRUE(absl::StartsWith(tcp->data(), "HTTP/1.1 503"));
}

class OriginalDstTcpProxyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  OriginalDstTcpProxyIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
    enableHalfClose(true);
  }
};

TEST_P(OriginalDstTcpProxyIntegrationTest, TestManyConnections) {
  // Windows apparently have this loopback property.
  DISABLE_UNDER_WINDOWS;
  if (version_ != Network::Address::IpVersion::v4) {
    return;
  }

  // Create an upstream on 0.0.0.0
  auto address = Network::Test::getAnyAddress(Network::Address::IpVersion::v4);
  ASSERT(!upstream_tls_);
  fake_upstreams_.emplace_back(
      std::make_unique<FakeUpstream>(Network::Test::createRawBufferDownstreamSocketFactory(),
                                     address, configWithType(upstreamProtocol())));

  config_helper_.addConfigModifier(
      setOriginalDstCluster(fake_upstreams_[0]->localAddress()->ip()->port()));
  initialize();

  const int32_t kMaxConnections = 10;
  for (int i = 0; i < kMaxConnections; ++i) {
    // Set up the connection
    std::string address_str = fmt::format("127.0.0.{}", i + 1);
    IntegrationTcpClientPtr tcp_client =
        makeTcpConnection(lookupPort("listener_0"), nullptr, nullptr, address_str);
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    EXPECT_EQ(address_str, fake_upstream_connection->connection()
                               .connectionInfoProvider()
                               .localAddress()
                               ->ip()
                               ->addressAsString());

    // Write bidirectional data.
    ASSERT_TRUE(fake_upstream_connection->write("hello"));
    tcp_client->waitForData("hello");
    ASSERT_TRUE(tcp_client->write("hello"));
    ASSERT_TRUE(fake_upstream_connection->waitForData(5));

    // Close down the connection.
    ASSERT_TRUE(fake_upstream_connection->write("", true));
    tcp_client->waitForHalfClose();
    ASSERT_TRUE(tcp_client->write("", true));
    ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }
  test_server_->waitForCounter("cluster_manager.cluster_updated", testing::Ge(kMaxConnections));
}

// The destination port of a connection whose port was translated before it reached Envoy can be set
// from filter state, and an upstream PROXY protocol header then carries that port.
TEST_P(OriginalDstTcpProxyIntegrationTest, LocalPortFromFilterState) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    auto* set_filter_state = listener->add_listener_filters();
    set_filter_state->set_name("envoy.filters.listener.set_filter_state");
    TestUtility::loadFromYaml(R"EOF(
"@type": type.googleapis.com/envoy.extensions.filters.listener.set_filter_state.v3.Config
on_accept:
- object_key: envoy.filters.listener.original_dst.local_port
  format_string:
    text_format_source:
      inline_string: "443"
)EOF",
                              *set_filter_state->mutable_typed_config());
    auto* original_dst = listener->add_listener_filters();
    original_dst->set_name("envoy.filters.listener.original_dst");
    std::ignore = original_dst->mutable_typed_config()->PackFrom(
        envoy::extensions::filters::listener::original_dst::v3::OriginalDst());

    envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocolUpstreamTransport
        proxy_protocol;
    proxy_protocol.mutable_config()->set_version(envoy::config::core::v3::ProxyProtocolConfig::V1);
    proxy_protocol.mutable_transport_socket()->set_name("envoy.transport_sockets.raw_buffer");
    std::ignore = proxy_protocol.mutable_transport_socket()->mutable_typed_config()->PackFrom(
        envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer());
    auto* transport_socket =
        bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
    transport_socket->set_name("envoy.transport_sockets.upstream_proxy_protocol");
    std::ignore = transport_socket->mutable_typed_config()->PackFrom(proxy_protocol);
  });
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(tcp_client->write("data"));
  std::string observed_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\ndata"), &observed_data));
  if (version_ == Network::Address::IpVersion::v4) {
    EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP4 127.0.0.1 127.0.0.1 "));
  } else {
    EXPECT_THAT(observed_data, testing::StartsWith("PROXY TCP6 ::1 ::1 "));
  }
  EXPECT_THAT(observed_data, testing::EndsWith(" 443\r\ndata"));

  ASSERT_TRUE(fake_upstream_connection->write("", true));
  tcp_client->waitForHalfClose();
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

INSTANTIATE_TEST_SUITE_P(IpVersions, OriginalDstTcpProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace Envoy
