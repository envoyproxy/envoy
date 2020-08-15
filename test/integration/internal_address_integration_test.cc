#include <memory>
#include <string>

#include "test/integration/integration.h"
#include "test/mocks/secret/mocks.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {

std::string tcpInternalConfig() {
  return fmt::format(R"EOF(
admin:
  access_log_path: {}
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  lds_config:
    path: {}
static_resources:
  secrets:
  - name: "secret_static_0"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES"
      private_key:
        inline_string: "DUMMY_INLINE_BYTES"
      password:
        inline_string: "DUMMY_INLINE_BYTES"
  clusters:
  - name: cluster_internal
    load_assignment:
      cluster_name: cluster_internal
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              envoy_internal_address:
                peer_listener_name: listener_internal
  - name: cluster_0
    load_assignment:
      cluster_name: cluster_0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  listeners:
  - name: tcp_proxy
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: tcp
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_internal
  - name: listener_internal
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: tcp
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0          
)EOF",
                     TestEnvironment::nullDevicePath(), TestEnvironment::nullDevicePath());
}

class InternalAddressIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public BaseIntegrationTest {
public:
  InternalAddressIntegrationTest() : BaseIntegrationTest(GetParam(), tcpInternalConfig()) {
    enable_half_close_ = true;
  }

public:
  // Called once by the gtest framework before any tests are run.
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
  }
  void initialize() override;
};

void InternalAddressIntegrationTest::initialize() { BaseIntegrationTest::initialize(); }

TEST_P(InternalAddressIntegrationTest, TcpProxyUpstreamWritesFirst) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("hell"));
  tcp_client->waitForData("hell");
  // Make sure inexact matches work also on data already received.
  tcp_client->waitForData("ell", false);

  // Make sure length based wait works for the data already received
  ASSERT_TRUE(tcp_client->waitForData(4));
  ASSERT_TRUE(tcp_client->waitForData(3));

  // Drain part of the received message
  tcp_client->clearData(1);
  tcp_client->waitForData("ell");
  ASSERT_TRUE(tcp_client->waitForData(3));

  ASSERT_TRUE(tcp_client->write("world"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  ASSERT_TRUE(fake_upstream_connection->write("", true));
  tcp_client->waitForHalfClose();
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(InternalAddressIntegrationTest, TcpProxyDownstreamWritesFirst) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hell"));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->waitForData(4));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  ASSERT_TRUE(fake_upstream_connection->close());

  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();
  EXPECT_EQ("world", tcp_client->data());
}

TEST_P(InternalAddressIntegrationTest, TcpProxyUpstreamDisconnect) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForHalfClose();
  tcp_client->close();

  EXPECT_EQ("world", tcp_client->data());
}

TEST_P(InternalAddressIntegrationTest, TcpProxyDownstreamDisconnect) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hell"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(4));
  ASSERT_TRUE(fake_upstream_connection->write("foobar"));
  tcp_client->waitForData("foobar");
  ASSERT_TRUE(tcp_client->write("world", true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(9));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect(true));
  tcp_client->waitForDisconnect();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, InternalAddressIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);
} // namespace Envoy