#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.h"

#include "test/integration/integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Geoip {
namespace {

const std::string DefaultConfig = R"EOF(
name: envoy.filters.network.geoip
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.geoip.v3.Geoip
  provider:
    name: envoy.geoip_providers.maxmind
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.geoip_providers.maxmind.v3.MaxMindConfig
      common_provider_config:
        geo_field_keys:
          country: "country"
          region: "region"
          city: "city"
          asn: "asn"
      city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
      asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
)EOF";

class GeoipFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public BaseIntegrationTest {
public:
  GeoipFilterIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GeoipFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GeoipFilterIntegrationTest, GeoipFilterProcessesConnection) {
  config_helper_.renameListener("tcp");
  config_helper_.addNetworkFilter(TestEnvironment::substitute(DefaultConfig));
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(tcp_client->connected());

  // Verify stats were incremented indicating the filter processed the connection.
  test_server_->waitForCounterEq("geoip.total", 1);

  tcp_client->close();
}

// Tests that the filter handles LDS updates correctly without crashing.
TEST_P(GeoipFilterIntegrationTest, GeoipFilterNoCrashOnLdsUpdate) {
  config_helper_.renameListener("tcp");
  config_helper_.addNetworkFilter(TestEnvironment::substitute(DefaultConfig));
  initialize();

  // LDS update to modify the listener and trigger corresponding drain.
  {
    ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
    new_config_helper.addConfigModifier(
        [](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          listener->mutable_listener_filters_timeout()->set_seconds(10);
        });
    new_config_helper.setLds("1");
    test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
    test_server_->waitForCounterEq("listener_manager.lds.update_success", 2);
    test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", 0);
  }

  // Connection after LDS update to verify filter still works and no crash occurs.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(tcp_client->connected());
  test_server_->waitForCounterEq("geoip.total", 1);

  // Second connection to verify continued operation.
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(tcp_client2->connected());
  test_server_->waitForCounterEq("geoip.total", 2);

  tcp_client->close();
  tcp_client2->close();
}

} // namespace
} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
