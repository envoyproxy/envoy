#include "envoy/extensions/filters/http/health_check/v3/health_check.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class HTTPTypedPerFilterConfigTest : public testing::Test, public HttpIntegrationTest {
public:
  HTTPTypedPerFilterConfigTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, Network::Address::IpVersion::v4) {}

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          envoy::extensions::filters::http::health_check::v3::HealthCheck health_check;
          health_check.mutable_pass_through_mode()->set_value(false);

          // The http health_check filter doesn't support per filter config. So specify one
          // and expect the exception will be raised.
          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          auto* config = virtual_host->mutable_typed_per_filter_config();
          (*config)["envoy.filters.http.health_check"].PackFrom(health_check);

          auto* filter = hcm.mutable_http_filters()->Add();
          filter->set_name("envoy.filters.http.health_check");
          filter->mutable_typed_config()->PackFrom(health_check);
          // keep router the last
          auto size = hcm.http_filters_size();
          hcm.mutable_http_filters()->SwapElements(size - 2, size - 1);
        });
    HttpIntegrationTest::initialize();
  }
};

TEST_F(HTTPTypedPerFilterConfigTest, RejectUnsupportedTypedPerFilterConfig) {
  EXPECT_DEATH(initialize(), "The filter envoy.filters.http.health_check doesn't support virtual "
                             "host-specific configurations");
}

} // namespace
} // namespace Envoy