#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {
namespace {

const std::string DefaultConfig = R"EOF(
name: envoy.filters.http.geoip
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.geoip.v3.Geoip
  provider:
    name: envoy.geoip_providers.maxmind
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.geoip_providers.maxmind.v3.MaxMindConfig
      common_provider_config:
        geo_headers_to_add:
          country: "x-geo-country"
          region: "x-geo-region"
          city: "x-geo-city"
          asn: "x-geo-asn"
      city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
      asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
)EOF";

const std::string ConfigWithXff = R"EOF(
name: envoy.filters.http.geoip
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.geoip.v3.Geoip
  xff_config:
    xff_num_trusted_hops: 1
  provider:
    name: envoy.geoip_providers.maxmind
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.geoip_providers.maxmind.v3.MaxMindConfig
      common_provider_config:
        geo_headers_to_add:
          country: "x-geo-country"
          region: "x-geo-region"
          city: "x-geo-city"
          asn: "x-geo-asn"
          anon: "x-geo-anon"
          anon_vpn: "x-geo-anon-vpn"
      city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
      asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
      anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
)EOF";

const std::string ConfigIspAndAsn = R"EOF(
  name: envoy.filters.http.geoip
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.geoip.v3.Geoip
    xff_config:
      xff_num_trusted_hops: 1
    provider:
      name: envoy.geoip_providers.maxmind
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.geoip_providers.maxmind.v3.MaxMindConfig
        common_provider_config:
          geo_headers_to_add:
            country: "x-geo-country"
            region: "x-geo-region"
            city: "x-geo-city"
            asn: "x-geo-asn"
            isp: "x-geo-isp"
            apple_private_relay: "x-geo-apple-private-relay"
        city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
        isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
        asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF";

const std::string ConfigIspAndCity = R"EOF(
    name: envoy.filters.http.geoip
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.geoip.v3.Geoip
      xff_config:
        xff_num_trusted_hops: 1
      provider:
        name: envoy.geoip_providers.maxmind
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.geoip_providers.maxmind.v3.MaxMindConfig
          common_provider_config:
            geo_headers_to_add:
              country: "x-geo-country"
              region: "x-geo-region"
              city: "x-geo-city"
              asn: "x-geo-asn"
              isp: "x-geo-isp"
              apple_private_relay: "x-geo-apple-private-relay"
          city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
          isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
  )EOF";

const std::string ConfigIsApplePrivateRelayOnly = R"EOF(
    name: envoy.filters.http.geoip
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.geoip.v3.Geoip
      xff_config:
        xff_num_trusted_hops: 1
      provider:
        name: envoy.geoip_providers.maxmind
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.geoip_providers.maxmind.v3.MaxMindConfig
          common_provider_config:
            geo_headers_to_add:
              apple_private_relay: "x-geo-apple-private-relay"
          isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
    )EOF";

class GeoipFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  GeoipFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  absl::string_view headerValue(const absl::string_view& header_name) const {
    return upstream_request_->headers()
        .get(Http::LowerCaseString(header_name))[0]
        ->value()
        .getStringView();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GeoipFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GeoipFilterIntegrationTest, GeoDataDontPopulatedWhenCalledFromLocalhosNoXff) {
  config_helper_.prependFilter(TestEnvironment::substitute(DefaultConfig));
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->headers().get(Http::LowerCaseString("x-geo-city")).empty());
  ASSERT_TRUE(response->headers().get(Http::LowerCaseString("x-geo-region")).empty());
  ASSERT_TRUE(response->headers().get(Http::LowerCaseString("x-geo-country")).empty());
  ASSERT_TRUE(response->headers().get(Http::LowerCaseString("x-geo-asn")).empty());
  ASSERT_TRUE(response->headers().get(Http::LowerCaseString("x-geo-anon-vpn")).empty());
  ASSERT_TRUE(response->headers().get(Http::LowerCaseString("x-geo-anon")).empty());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterEq("http.config_test.geoip.total", 1);
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.city_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.asn_db.total")->value());
  EXPECT_EQ(nullptr, test_server_->counter("http.config_test.maxmind.city_db.hit"));
  EXPECT_EQ(nullptr, test_server_->counter("http.config_test.maxmind.asn_db.hit"));
}

TEST_P(GeoipFilterIntegrationTest, GeoAnonDataPopulatedUseXff) {
  config_helper_.prependFilter(TestEnvironment::substitute(ConfigWithXff));
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "::81.2.69.0,9.10.11.12"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_EQ("true", headerValue("x-geo-anon"));
  EXPECT_EQ("true", headerValue("x-geo-anon-vpn"));
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterEq("http.config_test.geoip.total", 1);
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.anon_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.anon_db.hit")->value());
}

TEST_P(GeoipFilterIntegrationTest, GeoDataPopulatedUseXff) {
  config_helper_.prependFilter(TestEnvironment::substitute(ConfigWithXff));
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "216.160.83.56"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_EQ("Milton", headerValue("x-geo-city"));
  EXPECT_EQ("WA", headerValue("x-geo-region"));
  EXPECT_EQ("US", headerValue("x-geo-country"));
  EXPECT_EQ("209", headerValue("x-geo-asn"));
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterEq("http.config_test.geoip.total", 1);
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.city_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.city_db.hit")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.asn_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.asn_db.hit")->value());
}

TEST_P(GeoipFilterIntegrationTest, GeoDataPopulatedUseXffWithIspAndAsn) {
  config_helper_.prependFilter(TestEnvironment::substitute(ConfigIspAndAsn));
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "216.160.83.56,9.10.11.12"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_EQ("Milton", headerValue("x-geo-city"));
  EXPECT_EQ("WA", headerValue("x-geo-region"));
  EXPECT_EQ("US", headerValue("x-geo-country"));
  EXPECT_EQ("209", headerValue("x-geo-asn"));
  EXPECT_EQ("Century Link", headerValue("x-geo-isp"));
  EXPECT_EQ("false", headerValue("x-geo-apple-private-relay"));
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterEq("http.config_test.geoip.total", 1);
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.city_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.city_db.hit")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.isp_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.isp_db.hit")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.asn_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.asn_db.hit")->value());
}

TEST_P(GeoipFilterIntegrationTest, GeoDataPopulatedUseXffWithIsp) {
  config_helper_.prependFilter(TestEnvironment::substitute(ConfigIspAndCity));
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "216.160.83.56,9.10.11.12"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_EQ("Milton", headerValue("x-geo-city"));
  EXPECT_EQ("WA", headerValue("x-geo-region"));
  EXPECT_EQ("US", headerValue("x-geo-country"));
  EXPECT_EQ("209", headerValue("x-geo-asn"));
  EXPECT_EQ("Century Link", headerValue("x-geo-isp"));
  EXPECT_EQ("false", headerValue("x-geo-apple-private-relay"));
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterEq("http.config_test.geoip.total", 1);
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.city_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.city_db.hit")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.isp_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.isp_db.hit")->value());
  // asn_db is not used so the metrics should be null.
  EXPECT_EQ(nullptr, test_server_->counter("http.config_test.maxmind.asn_db.total"));
  EXPECT_EQ(nullptr, test_server_->counter("http.config_test.maxmind.asn_db.hit"));
}

TEST_P(GeoipFilterIntegrationTest, GeoHeadersOverridenInRequest) {
  config_helper_.prependFilter(TestEnvironment::substitute(ConfigWithXff));
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "216.160.83.56,9.10.11.12"},
                                                 {"x-geo-city", "Berlin"},
                                                 {"x-geo-country", "Germany"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_EQ("Milton", headerValue("x-geo-city"));
  EXPECT_EQ("US", headerValue("x-geo-country"));
  ASSERT_TRUE(response->complete());
  test_server_->waitForCounterEq("http.config_test.geoip.total", 1);
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.city_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.city_db.hit")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.asn_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.asn_db.hit")->value());
}

TEST_P(GeoipFilterIntegrationTest, GeoDataNotPopulatedOnEmptyLookupResult) {
  config_helper_.prependFilter(TestEnvironment::substitute(ConfigWithXff));
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "10.10.10.10,9.10.11.12"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  // 10.10.10.10 is a private IP and is absent in test_data/GeoIP2-Anonymous-IP-Test.mmdb database.
  ASSERT_TRUE(response->headers().get(Http::LowerCaseString("x-geo-anon")).empty());
  ASSERT_TRUE(response->headers().get(Http::LowerCaseString("x-geo-anon-vpn")).empty());
  test_server_->waitForCounterEq("http.config_test.geoip.total", 1);
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.anon_db.total")->value());
  EXPECT_EQ(nullptr, test_server_->counter("http.config_test.maxmind.anon_db.hit"));
}

TEST_P(GeoipFilterIntegrationTest, GeoipFilterNoCrashOnLdsUpdate) {
  config_helper_.prependFilter(TestEnvironment::substitute(ConfigWithXff));
  initialize();

  // LDS update to modify the listener and corresponding drain.
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
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "216.160.83.56,9.10.11.12"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_EQ("Milton", headerValue("x-geo-city"));
  EXPECT_EQ("WA", headerValue("x-geo-region"));
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  auto response2 = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterEq("http.config_test.geoip.total", 2);
  EXPECT_EQ(2, test_server_->counter("http.config_test.maxmind.city_db.total")->value());
  EXPECT_EQ(2, test_server_->counter("http.config_test.maxmind.city_db.hit")->value());
}

TEST_P(GeoipFilterIntegrationTest, OnlyApplePrivateRelayHeaderIsPopulated) {
  config_helper_.prependFilter(TestEnvironment::substitute(ConfigIsApplePrivateRelayOnly));
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "::65.116.3.80,9.10.11.12"},
                                                 {"x-geo-city", "Berlin"},
                                                 {"x-geo-country", "Germany"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_EQ("false", headerValue("x-geo-apple-private-relay"));
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  test_server_->waitForCounterEq("http.config_test.geoip.total", 1);
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.isp_db.total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.maxmind.isp_db.hit")->value());
}

} // namespace
} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
