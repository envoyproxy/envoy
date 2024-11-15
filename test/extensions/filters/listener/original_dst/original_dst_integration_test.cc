#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
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
    listener_filter->mutable_typed_config()->PackFrom(original_dst);
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
  test_server_->waitForCounterGe("cluster_manager.cluster_updated", kMaxConnections);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, OriginalDstTcpProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace Envoy
