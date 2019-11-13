#include "extensions/filters/network/rbac/config.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"

#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBAC {
namespace {

std::string rbac_config;

} // namespace

class RoleBasedAccessControlNetworkFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  RoleBasedAccessControlNetworkFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), rbac_config) {}

  static void SetUpTestSuite() {
    rbac_config = ConfigHelper::BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
       -  name: envoy.filters.network.rbac
          typed_config:
            "@type": type.googleapis.com/envoy.config.filter.network.rbac.v2.RBAC
            stat_prefix: tcp.
            rules:
              policies:
                "foo":
                  permissions:
                    - any: true
                  principals:
                    - not_id:
                        any: true
       -  name: envoy.echo
          config:
)EOF";
  }

  void initializeFilter(const std::string& config) {
    config_helper_.addConfigModifier([config](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      envoy::api::v2::listener::Filter filter;
      TestUtility::loadFromYaml(config, filter);
      ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
      auto l = bootstrap.mutable_static_resources()->mutable_listeners(0);
      ASSERT_GT(l->filter_chains_size(), 0);
      ASSERT_GT(l->filter_chains(0).filters_size(), 0);
      l->mutable_filter_chains(0)->mutable_filters(0)->Swap(&filter);
    });

    BaseIntegrationTest::initialize();
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RoleBasedAccessControlNetworkFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, Allowed) {
  initializeFilter(R"EOF(
name: envoy.filters.network.rbac
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.network.rbac.v2.RBAC
  stat_prefix: tcp.
  rules:
    policies:
      "allow_all":
        permissions:
          - any: true
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
)EOF");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write("hello");
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();

  test_server_->waitForCounterGe("tcp.rbac.allowed", 1);
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.denied")->value());
  EXPECT_EQ(0U, test_server_->counter("tcp.rbac.shadow_allowed")->value());
  test_server_->waitForCounterGe("tcp.rbac.shadow_denied", 1);
}

TEST_P(RoleBasedAccessControlNetworkFilterIntegrationTest, Denied) {
  initializeFilter(R"EOF(
name: envoy.filters.network.rbac
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.network.rbac.v2.RBAC
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
      "allow_all":
        permissions:
          - any: true
        principals:
          - any: true
)EOF");
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write("hello");
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
