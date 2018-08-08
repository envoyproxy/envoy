#include "extensions/filters/network/rbac/config.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBAC {

std::string rbac_config;

class RoleBasedAccessControlNetworkFilterIntegrationTest
    : public BaseIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  RoleBasedAccessControlNetworkFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), rbac_config) {}

  static void SetUpTestCase() {
    rbac_config = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
  listeners:
  - name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 8000
    filter_chains:
      filters:
      - name: envoy.filters.network.rbac
        config:
          stat_prefix: tcp.
          rules:
            policies:
              "allow_8080":
                permissions:
                  - destination_port: 8000
                principals:
                  - any: true
          shadow_rules:
            policies:
              "deny_all":
                permissions:
                  - any: true
                principals:
                  - not_id:
                      any: true
  - name: listener_1
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 8080
    filter_chains:
      filters:
      - name: envoy.filters.network.rbac
        config:
          stat_prefix: tcp.
          rules:
            policies:
              "deny_all":
                permissions:
                  - any: true
                principals:
                  - not_id:
                      any: true
          shadow_rules:
            policies:
              "allow_8080":
                permissions:
                  - destination_port: 8080
                principals:
                  - any: true
    )EOF";
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, RoleBasedAccessControlNetworkFilterIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, Allowed) {
  BaseIntegrationTest::initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(8000);
  tcp_client->write("hello");
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();

  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.allowed")->value());
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.denied")->value());
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.shadow_allowed")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.shadow_denied")->value());
}

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, Denied) {
  BaseIntegrationTest::initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(8080);
  tcp_client->write("hello");
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->waitForDisconnect();

  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.allowed")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.denied")->value());
  EXPECT_EQ(1U, test_server_->counter("tcp.rbac.shadow_allowed")->value());
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.shadow_denied")->value());
}

} // namespace RBAC
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
