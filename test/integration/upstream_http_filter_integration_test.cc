#include <ostream>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/config/utility.h"
#include "test/integration/filters/repick_cluster_filter.h"
#include "test/integration/http_integration.h"

#include "http_integration.h"

namespace Envoy {
namespace {

using HttpFilterProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;
class UpstreamHttpFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  UpstreamHttpFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    autonomous_upstream_ = true;
  }

  void addFilters(const std::vector<HttpFilterProto>& cluster_upstream_filters,
                  const std::vector<HttpFilterProto>& router_upstream_filters) {
    if (!cluster_upstream_filters.empty()) {
      config_helper_.addConfigModifier(
          [cluster_upstream_filters](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
            auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
            ConfigHelper::HttpProtocolOptions protocol_options =
                MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
                    (*cluster->mutable_typed_extension_protocol_options())
                        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
            for (const auto& filter : cluster_upstream_filters) {
              *protocol_options.add_http_filters() = filter;
            }
            (*cluster->mutable_typed_extension_protocol_options())
                ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                    .PackFrom(protocol_options);
            // std::cout << cluster->DebugString() << std::endl;
            // std::cout << bootstrap.DebugString() << std::endl;
          });
    }
    if (!router_upstream_filters.empty()) {
      config_helper_.addConfigModifier(
          [router_upstream_filters](envoy::extensions::filters::network::http_connection_manager::
                                        v3::HttpConnectionManager& hcm) -> void {
            HttpFilterProto& router_filter = *hcm.mutable_http_filters(0);
            ASSERT_EQ(router_filter.name(), "envoy.filters.http.router");
            envoy::extensions::filters::http::router::v3::Router router;
            router_filter.typed_config().UnpackTo(&router);
            for (const auto& filter : router_upstream_filters) {
              *router.add_upstream_http_filters() = filter;
            }
            router_filter.mutable_typed_config()->PackFrom(router);
            std::cout << hcm.DebugString() << std::endl;
          });
    }
  }
};

TEST_P(UpstreamHttpFilterIntegrationTest, ClusterOnlyFilters) {
  HttpFilterProto add_header_filter;
  add_header_filter.set_name("add-header-filter");
  HttpFilterProto codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  addFilters({add_header_filter, codec_filter}, {});
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  std::cout << *upstream_headers;
  ASSERT_TRUE(upstream_headers != nullptr);
  cleanupUpstreamAndDownstream();
  EXPECT_FALSE(upstream_headers->get(Http::LowerCaseString("x-header-to-add")).empty());
}

TEST_P(UpstreamHttpFilterIntegrationTest, RouterOnlyFilters) {
  HttpFilterProto add_header_filter;
  add_header_filter.set_name("add-header-filter");
  HttpFilterProto codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  addFilters({}, {add_header_filter, codec_filter});
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  cleanupUpstreamAndDownstream();
  EXPECT_FALSE(upstream_headers->get(Http::LowerCaseString("x-header-to-add")).empty());
}

// Only cluster-specified filters should be applied.
TEST_P(UpstreamHttpFilterIntegrationTest, RouterAndClusterFilters) {
  HttpFilterProto add_header_filter;
  add_header_filter.set_name("add-header-filter");
  HttpFilterProto add_body_filter;
  add_body_filter.set_name("add-body-filter");
  HttpFilterProto codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  addFilters({add_header_filter, codec_filter}, {add_body_filter, codec_filter});
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  auto upstream_headers =
      reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get())->lastRequestHeaders();
  ASSERT_TRUE(upstream_headers != nullptr);
  cleanupUpstreamAndDownstream();
  EXPECT_FALSE(upstream_headers->get(Http::LowerCaseString("x-header-to-add")).empty());
}

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamHttpFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace Envoy
