#include "envoy/config/filter/network/source_ip_access/v2/source_ip_access.pb.validate.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

namespace Envoy {

std::string config;

class SourceIpAccessIntegrationTest : public BaseIntegrationTest,
                                      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  SourceIpAccessIntegrationTest() : BaseIntegrationTest(GetParam(), config) {}

  // Called once by the gtest framework before any EchoIntegrationTests are run.
  static void SetUpTestCase() {
    config = ConfigHelper::BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
      - name: envoy.source_ip_access
        config:
          stat_prefix: my_stat_prefix
          allow_by_default: true
      - name: envoy.tcp_proxy
        config:
          stat_prefix: tcp_stats
          cluster: cluster_0
      )EOF";
  }

  void setTestConfig(bool allow_by_default, bool add_exceptions) {
    config_helper_.addConfigModifier([this, allow_by_default,
                                      add_exceptions](envoy::config::bootstrap::v2::Bootstrap& bs) {
      auto* listener = bs.mutable_static_resources()->mutable_listeners(0);
      auto* config = listener->mutable_filter_chains(0)->mutable_filters(0)->mutable_config();

      envoy::config::filter::network::source_ip_access::v2::SourceIpAccess source_ip_access_config;

      MessageUtil::jsonConvert(*config, source_ip_access_config);
      source_ip_access_config.set_allow_by_default(allow_by_default);

      if (add_exceptions) {
        auto exception = source_ip_access_config.add_exception_prefixes();
        if (GetParam() == Network::Address::IpVersion::v4) {
          exception->set_address_prefix("127.0.0.1");
          exception->mutable_prefix_len()->set_value(32);
        } else {
          exception->set_address_prefix("::1");
          exception->mutable_prefix_len()->set_value(128);
        }
      }

      MessageUtil::jsonConvert(source_ip_access_config, *config);
    });
    initialize();
  }

  /**
   * Initializer for an individual test.
   */
  void SetUp() override { config_helper_.renameListener("source_ip_access"); }

  /**
   *  Destructor for an individual test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void testAllow() {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("source_ip_access"));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

    tcp_client->write("hello");
    ASSERT_TRUE(fake_upstream_connection->waitForData(5));

    ASSERT_TRUE(fake_upstream_connection->write("world"));
    tcp_client->waitForData("world");

    tcp_client->close();
  }

  void testDeny() {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("source_ip_access"));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection,
                                                          std::chrono::milliseconds(100)));

    tcp_client->waitForDisconnect();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, SourceIpAccessIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(SourceIpAccessIntegrationTest, AllowThroughDefault) {
  setTestConfig(true, false);
  testAllow();
}

TEST_P(SourceIpAccessIntegrationTest, AllowThroughException) {
  setTestConfig(false, true);
  testAllow();
}

TEST_P(SourceIpAccessIntegrationTest, DenyThroughDefault) {
  setTestConfig(false, false);
  testDeny();
}

TEST_P(SourceIpAccessIntegrationTest, DenyThroughException) {
  setTestConfig(true, true);
  testDeny();
}
} // namespace Envoy
