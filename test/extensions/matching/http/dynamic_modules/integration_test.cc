#include <filesystem>

#include "envoy/extensions/matching/http/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"

namespace Envoy {

// Drives the data input end to end. The module echoes the ``x-route-key`` request header,
// and an exact match map dispatches that value to a route, so the module selects the route with a
// single evaluation and without clearing the route cache.
class DynamicModuleDataInputIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleDataInputIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void initializeWithDataInput() {
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("matcher_string_input_echo", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* route_config = hcm.mutable_route_config();
          route_config->clear_virtual_hosts();

          constexpr auto vhost_yaml = R"EOF(
name: matcher_vhost
domains: ["*"]
matcher:
  matcher_tree:
    input:
      name: envoy.matching.inputs.dynamic_module_string_data_input
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.http.dynamic_modules.v3.DynamicModuleDataInput
        dynamic_module_config:
          name: matcher_string_input_echo
          do_not_close: true
        input_name: route_selector
        input_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: x-route-key
    exact_match_map:
      map:
        cluster_0:
          action:
            name: route
            typed_config:
              "@type": type.googleapis.com/envoy.config.route.v3.Route
              match:
                prefix: /
              route:
                cluster: cluster_0
)EOF";

          envoy::config::route::v3::VirtualHost virtual_host;
          TestUtility::loadFromYaml(vhost_yaml, virtual_host);
          route_config->add_virtual_hosts()->CopyFrom(virtual_host);
        });

    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleDataInputIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The value the module extracts selects a route through the exact match map.
TEST_P(DynamicModuleDataInputIntegrationTest, MatchingRouteKey) {
  initializeWithDataInput();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-route-key", "cluster_0"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_TRUE(upstream_request_->complete());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

// A value with no entry in the exact match map does not select a route.
TEST_P(DynamicModuleDataInputIntegrationTest, NonMatchingRouteKey) {
  initializeWithDataInput();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-route-key", "cluster_unknown"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
}

// A request that omits the header leaves the input without a value, so no route is selected.
TEST_P(DynamicModuleDataInputIntegrationTest, MissingRouteKey) {
  initializeWithDataInput();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
}

} // namespace Envoy
