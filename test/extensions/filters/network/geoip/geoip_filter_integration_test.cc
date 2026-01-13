#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.h"

#include "source/common/router/string_accessor_impl.h"

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
  stat_prefix: ""
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

// Filter state object factory for the custom client IP key used in integration tests.
class ClientIpObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "test.geoip.client_ip"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(ClientIpObjectFactory, StreamInfo::FilterState::ObjectFactory);

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

// Tests that the filter uses client IP from filter state via formatter and stores correct
// geolocation data.
TEST_P(GeoipFilterIntegrationTest, GeoipFilterUsesClientIpFromFormatter) {
  // IP address 2.125.160.216 is a test IP in GeoLite2-City-Test.mmdb that resolves to
  // England, GB.
  const std::string set_filter_state_config = R"EOF(
name: envoy.filters.network.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
  on_new_connection:
  - object_key: test.geoip.client_ip
    format_string:
      text_format_source:
        inline_string: "2.125.160.216"
)EOF";

  const std::string geoip_config = R"EOF(
name: envoy.filters.network.geoip
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.geoip.v3.Geoip
  stat_prefix: ""
  client_ip: "%FILTER_STATE(test.geoip.client_ip:PLAIN)%"
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

  useListenerAccessLog("%FILTER_STATE(envoy.geoip:PLAIN)%");
  config_helper_.renameListener("tcp");
  // addNetworkFilter prepends, so add geoip first, then set_filter_state.
  config_helper_.addNetworkFilter(TestEnvironment::substitute(geoip_config));
  config_helper_.addNetworkFilter(set_filter_state_config);
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(tcp_client->connected());

  // Wait for geoip lookup to complete before closing connection.
  test_server_->waitForCounterEq("geoip.total", 1);

  tcp_client->close();
  test_server_.reset();

  // Verify filter state contains correct geolocation data for IP 2.125.160.216.
  std::string access_log = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(access_log, testing::HasSubstr("\"country\":\"GB\""));
  EXPECT_THAT(access_log, testing::HasSubstr("\"city\":"));
  EXPECT_THAT(access_log, testing::HasSubstr("\"region\":"));
}

// Tests that the filter uses a static IP from the formatter.
TEST_P(GeoipFilterIntegrationTest, GeoipFilterUsesStaticIpFromFormatter) {
  // Use a static IP address directly in the formatter.
  const std::string geoip_config = R"EOF(
name: envoy.filters.network.geoip
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.geoip.v3.Geoip
  stat_prefix: ""
  client_ip: "2.125.160.216"
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

  useListenerAccessLog("%FILTER_STATE(envoy.geoip:PLAIN)%");
  config_helper_.renameListener("tcp");
  config_helper_.addNetworkFilter(TestEnvironment::substitute(geoip_config));
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(tcp_client->connected());

  // Wait for geoip lookup to complete before closing connection.
  test_server_->waitForCounterEq("geoip.total", 1);

  tcp_client->close();
  test_server_.reset();

  // Verify filter state contains correct geolocation data for IP 2.125.160.216.
  std::string access_log = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(access_log, testing::HasSubstr("\"country\":\"GB\""));
  EXPECT_THAT(access_log, testing::HasSubstr("\"city\":"));
  EXPECT_THAT(access_log, testing::HasSubstr("\"region\":"));
}

// Tests that the filter falls back to connection address when formatter returns empty.
TEST_P(GeoipFilterIntegrationTest, GeoipFilterFallsBackToConnectionAddress) {
  // Configure with a filter state key that doesn't exist - formatter will return "-".
  const std::string geoip_config = R"EOF(
name: envoy.filters.network.geoip
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.geoip.v3.Geoip
  stat_prefix: ""
  client_ip: "%FILTER_STATE(nonexistent.filter.state.key:PLAIN)%"
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

  config_helper_.renameListener("tcp");
  config_helper_.addNetworkFilter(TestEnvironment::substitute(geoip_config));
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(tcp_client->connected());

  // Verify stats were incremented indicating the filter processed the connection.
  // The filter should fall back to connection remote address when formatter returns empty.
  test_server_->waitForCounterEq("geoip.total", 1);

  tcp_client->close();
}

} // namespace
} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
