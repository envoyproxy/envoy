#include <string>

#include "source/extensions/filters/network/ext_proc/config.h"

#include "test/config/utility.h"
#include "test/integration/base_integration_test.h"
#include "test/integration/fake_upstream.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

class NetworkExtProcFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  NetworkExtProcFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), absl::StrCat(Envoy::ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        - name: envoy.network_ext_proc.ext_proc_filter
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.ext_proc.v3.NetworkExternalProcessor
            grpc_service:
              envoy_grpc:
                cluster_name: "cluster_1"
        - name: envoy.filters.network.tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            stat_prefix: tcpproxy_stats
            cluster: cluster_0
)EOF")) {
    enableHalfClose(true);
  }

  void initialize() override {
    config_helper_.renameListener("network_ext_proc");
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, NetworkExtProcFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test downstream connection closing
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyDownstreamClose) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc"));
  ASSERT_TRUE(tcp_client->write("client_data", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->waitForData(11));

  tcp_client->close();
}

// Test that data is passed through both directions
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyBidirectional) {
  initialize();

  // Connect to the server
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc"));

  // Write data from client to server
  ASSERT_TRUE(tcp_client->write("client_data", false));

  // Wait for the upstream connection and verify data
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(11));

  // Write data from server to client
  ASSERT_TRUE(fake_upstream_connection->write("server_response", false));

  // Verify client received the data
  tcp_client->waitForData("server_response");

  // Close everything
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
