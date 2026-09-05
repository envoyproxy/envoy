#include "envoy/extensions/http/early_header_mutation/exact_path_rewrite/v3/exact_path_rewrite.pb.h"
#include "envoy/extensions/http/early_header_mutation/exact_path_rewrite/v3/exact_path_rewrite.pb.validate.h"

#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

using ProtoExactPathRewrite =
    envoy::extensions::http::early_header_mutation::exact_path_rewrite::v3::ExactPathRewrite;

class ExactPathRewriteIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                        public HttpIntegrationTest {
public:
  ExactPathRewriteIntegrationTest()
      : HttpIntegrationTest(Envoy::Http::CodecType::HTTP1, GetParam()) {}

  // `exact_path_rewrite_yaml` configures the extension. `route_path` is the exact
  // path the single default route is set up to match, so tests can assert that
  // route lookup only succeeds once the extension has rewritten the path to it.
  void initializeExtension(const std::string& exact_path_rewrite_yaml,
                           const std::string& route_path) {
    config_helper_.addConfigModifier(
        [exact_path_rewrite_yaml, route_path](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          ProtoExactPathRewrite proto_config;
          TestUtility::loadFromYaml(exact_path_rewrite_yaml, proto_config);

          // Load extension.
          auto* new_extension = hcm.add_early_header_mutation_extensions();
          new_extension->set_name("test");
          std::ignore = new_extension->mutable_typed_config()->PackFrom(proto_config);

          hcm.mutable_route_config()
              ->mutable_virtual_hosts(0)
              ->mutable_routes(0)
              ->mutable_match()
              ->set_path(route_path);
        });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ExactPathRewriteIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ExactPathRewriteIntegrationTest, RewritesPathBeforeRouteLookup) {
  const std::string exact_path_rewrite_yaml = R"EOF(
  host_header: ":authority"
  hosts:
  - domains: ["rewrite.example.com"]
    rules:
    - exact_path: "/old"
      replacement_path: "/new"
  )EOF";

  // Only the rewritten path matches the route, so the route lookup only
  // succeeds if the extension already rewrote the path.
  initializeExtension(exact_path_rewrite_yaml, "/new");

  // The authority matches a configured domain, so the path is rewritten from
  // "/old" to "/new" before route lookup and the exact-path route matches.
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Envoy::Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                          {":path", "/old"},
                                                          {":scheme", "http"},
                                                          {":authority", "rewrite.example.com"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    waitForNextUpstreamRequest();
    EXPECT_EQ("/new", upstream_request_->headers().getPathValue());
    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());

    cleanupUpstreamAndDownstream();
  }

  // The authority does not match any configured domain, so the path is left
  // untouched and the exact-path route no longer matches "/old".
  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Envoy::Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                          {":path", "/old"},
                                                          {":scheme", "http"},
                                                          {":authority", "other.example.com"}};
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("404", response->headers().getStatusValue());
  }
}

} // namespace
} // namespace Envoy
