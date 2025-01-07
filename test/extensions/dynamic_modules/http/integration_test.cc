#include "test/integration/http_integration.h"

namespace Envoy {
class DynamicModulesIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  DynamicModulesIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()){};

  void initializeFilter(const std::string& module_name, const std::string& filter_name,
                        const std::string& config = "") {
    constexpr auto filter_config = R"EOF(
name: envoy.extensions.filters.http.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter
  dynamic_module_config:
    name: {}
  filter_name: {}
  filter_config: {}
)EOF";

    config_helper_.prependFilter(fmt::format(filter_config, module_name, filter_name, config));
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesIntegrationTest, Nop) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
      1);

  initializeFilter("http", "passthrough");
  // Create a client aimed at Envoy’s default HTTP port.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Create some request headers.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  // Send the request headers from the client, wait until they are received upstream. When they
  // are received, send the default response headers from upstream and wait until they are
  // received at by client
  auto response = sendRequestAndWaitForResponse(request_headers, 10, default_response_headers_, 10);

  // Verify the proxied request was received upstream, as expected.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(10U, upstream_request_->bodyLength());
  // Verify the proxied response was received downstream, as expected.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(10U, response->body().size());
}

} // namespace Envoy
