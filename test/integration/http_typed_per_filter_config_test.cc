#include "envoy/config/route/v3/route_components.pb.h"

#include "test/integration/filters/set_route_filter_config.pb.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class HTTPTypedPerFilterConfigTest : public testing::Test, public HttpIntegrationTest {
public:
  HTTPTypedPerFilterConfigTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, Network::Address::IpVersion::v4) {}
};

TEST_F(HTTPTypedPerFilterConfigTest, RejectUnsupportedTypedPerFilterConfig) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        test::integration::filters::SetRouteFilterConfig set_route_config;

        auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        auto* config = virtual_host->mutable_typed_per_filter_config();
        (*config)["set-route-filter"].PackFrom(set_route_config);

        auto* filter = hcm.mutable_http_filters()->Add();
        filter->set_name("set-route-filter");
        filter->mutable_typed_config()->PackFrom(set_route_config);
        // keep router the last
        auto size = hcm.http_filters_size();
        hcm.mutable_http_filters()->SwapElements(size - 2, size - 1);
      });
  EXPECT_DEATH(initialize(), "The filter set-route-filter doesn't support virtual host or "
                             "route specific configurations");
}

TEST_F(HTTPTypedPerFilterConfigTest, RejectUnknownHttpFilterInTypedPerFilterConfig) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        auto* config = virtual_host->mutable_typed_per_filter_config();
        (*config)["filter.unknown"].PackFrom(Envoy::ProtobufWkt::Struct());
      });
  EXPECT_DEATH(initialize(),
               "Didn't find a registered implementation for 'filter.unknown' with type URL: "
               "'google.protobuf.Struct'");
}

TEST_F(HTTPTypedPerFilterConfigTest, IgnoreUnknownOptionalHttpFilterInTypedPerFilterConfig) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
        auto* config = virtual_host->mutable_typed_per_filter_config();

        envoy::config::route::v3::FilterConfig filter_config;
        filter_config.set_is_optional(true);
        filter_config.mutable_config()->PackFrom(Envoy::ProtobufWkt::Struct());
        (*config)["filter.unknown"].PackFrom(filter_config);

        auto* filter = hcm.mutable_http_filters()->Add();
        filter->set_name("filter.unknown");
        filter->set_is_optional(true);
        // keep router the last
        auto size = hcm.http_filters_size();
        hcm.mutable_http_filters()->SwapElements(size - 2, size - 1);
      });
  initialize();
}

} // namespace
} // namespace Envoy
