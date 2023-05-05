#include <memory>
#include <string>

#include "source/common/network/utility.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {

class TcpProxyManyConnectionsTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public BaseIntegrationTest {
public:
  TcpProxyManyConnectionsTest() : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
    enableHalfClose(true);
  }

  void initialize() override {
    config_helper_.renameListener("tcp_proxy");
    BaseIntegrationTest::initialize();
  }

  // Multiplier for increasing timeouts in this test. It was empirically determined by running a
  // debug build with CPU oversubscribed by a factor of 5 on 3GHz CPU.
  static constexpr int timeout_scaling_factor_ = 20;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TcpProxyManyConnectionsTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TcpProxyManyConnectionsTest, TcpProxyManyConnections) {
  autonomous_upstream_ = true;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();
    for (int i = 0; i < static_resources->clusters_size(); ++i) {
      auto* cluster = static_resources->mutable_clusters(i);
      cluster->mutable_connect_timeout()->set_seconds(timeout_scaling_factor_);
      auto* thresholds = cluster->mutable_circuit_breakers()->add_thresholds();
      thresholds->mutable_max_connections()->set_value(1027);
      thresholds->mutable_max_pending_requests()->set_value(1027);
    }
  });
  initialize();
// The large number of connection is meant to regression test
// https://github.com/envoyproxy/envoy/issues/19033 but fails on apple CI
// TODO(alyssawilk) debug.
#if defined(__APPLE__)
  const int num_connections = 50;
#else
  const int num_connections = 1026;
#endif
  std::vector<IntegrationTcpClientPtr> clients(num_connections);

  for (int i = 0; i < num_connections; ++i) {
    clients[i] = makeTcpConnection(lookupPort("tcp_proxy"));
    test_server_->waitForGaugeGe("cluster.cluster_0.upstream_cx_active", i,
                                 TestUtility::DefaultTimeout * timeout_scaling_factor_);
  }
  for (int i = 0; i < num_connections; ++i) {
    IntegrationTcpClientPtr& tcp_client = clients[i];
    // The autonomous upstream is an HTTP upstream, so send raw HTTP.
    // This particular request will result in the upstream sending a response,
    // and flush-closing due to the 'close_after_response' header.
    ASSERT_TRUE(tcp_client->write(
        "GET / HTTP/1.1\r\nHost: foo\r\nclose_after_response: yes\r\ncontent-length: 0\r\n\r\n",
        false));
    tcp_client->waitForHalfClose(TestUtility::DefaultTimeout * timeout_scaling_factor_);
    tcp_client->close();
    EXPECT_THAT(tcp_client->data(), HasSubstr("aaaaaaaaaa"));
  }
}

} // namespace Envoy
