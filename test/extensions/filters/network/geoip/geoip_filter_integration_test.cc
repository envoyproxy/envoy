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

  void SetUp() override {
    config_helper_.renameListener("tcp");
    config_helper_.addNetworkFilter(TestEnvironment::substitute(DefaultConfig));
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GeoipFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GeoipFilterIntegrationTest, GeoipFilterProcessesConnection) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp"));
  ASSERT_TRUE(tcp_client->connected());

  // Verify stats were incremented indicating the filter processed the connection.
  test_server_->waitForCounterEq("geoip.total", 1);

  tcp_client->close();
}

} // namespace
} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
