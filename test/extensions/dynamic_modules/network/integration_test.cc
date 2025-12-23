#include "envoy/extensions/filters/network/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

namespace Envoy {

class DynamicModulesNetworkIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  DynamicModulesNetworkIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {
    skip_tag_extraction_rule_check_ = true;
    enableHalfClose(true);
  }

  void initializeFilter(const std::string& filter_name, const std::string& config = "") {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);

    config_helper_.addConfigModifier(
        [filter_name, config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* filter_chain = listener->mutable_filter_chains(0);

          // Get the existing tcp_proxy filter.
          auto tcp_proxy_filter = filter_chain->filters(0);

          // Clear and rebuild with dynamic modules first, then tcp_proxy.
          filter_chain->clear_filters();

          // Add the dynamic module filter.
          envoy::extensions::filters::network::dynamic_modules::v3::DynamicModuleNetworkFilter
              dm_config;
          dm_config.mutable_dynamic_module_config()->set_name("network_no_op");
          dm_config.set_filter_name(filter_name);
          if (!config.empty()) {
            dm_config.mutable_filter_config()->PackFrom(ValueUtil::stringValue(config));
          }

          auto* dm_filter = filter_chain->add_filters();
          dm_filter->set_name("envoy.filters.network.dynamic_modules");
          dm_filter->mutable_typed_config()->PackFrom(dm_config);

          // Add the tcp_proxy back.
          filter_chain->add_filters()->CopyFrom(tcp_proxy_filter);
        });

    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesNetworkIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesNetworkIntegrationTest, PassThrough) {
  initializeFilter("passthrough");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data from client to upstream.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));

  // Send data from upstream to client.
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  // Half-close to properly close the connection.
  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkIntegrationTest, LargeData) {
  initializeFilter("passthrough");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send large data from client to upstream with half-close.
  std::string large_data(100000, 'x');
  ASSERT_TRUE(tcp_client->write(large_data, true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(100000));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  // Close from upstream.
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(DynamicModulesNetworkIntegrationTest, HalfClose) {
  initializeFilter("passthrough");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->connected());

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data and half-close from client.
  ASSERT_TRUE(tcp_client->write("hello", true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  // Send data and close from upstream.
  ASSERT_TRUE(fake_upstream_connection->write("world", true));
  tcp_client->waitForData("world");
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

} // namespace Envoy
