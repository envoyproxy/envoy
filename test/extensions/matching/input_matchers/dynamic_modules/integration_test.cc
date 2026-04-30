#include "envoy/extensions/matching/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/matching/input_matchers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

// Parameterized over (language, ip_version). The C variant uses the matcher_check_headers
// fake; rust and go each ship a matcher_integration_test module that registers the same
// "header_check" matcher (header name from config; matches when value equals "match").
struct MatcherIntegrationParam {
  std::string language;
  Network::Address::IpVersion ip_version;
};

class DynamicModuleMatcherIntegrationTest : public testing::TestWithParam<MatcherIntegrationParam>,
                                            public HttpIntegrationTest {
public:
  DynamicModuleMatcherIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam().ip_version) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  // Returns (module_name, matcher_name) for the active language.
  std::pair<std::string, std::string> moduleAndMatcher() {
    if (GetParam().language == "c") {
      return {"matcher_check_headers", "header_check"};
    }
    return {"matcher_integration_test", "header_check"};
  }

  void initializeWithMatcher() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam().language),
        1);
    TestEnvironment::setEnvVar("GODEBUG", "cgocheck=0", 1);

    auto [module_name, matcher_name] = moduleAndMatcher();

    config_helper_.addConfigModifier(
        [module_name, matcher_name](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* route_config = hcm.mutable_route_config();
          route_config->clear_virtual_hosts();

          const std::string vhost_yaml = fmt::format(R"EOF(
name: matcher_vhost
domains: ["*"]
matcher:
  matcher_list:
    matchers:
    - predicate:
        single_predicate:
          input:
            name: envoy.matching.inputs.dynamic_module_data_input
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.matching.http.dynamic_modules.v3.HttpDynamicModuleMatchInput
          custom_match:
            name: envoy.matching.matchers.dynamic_modules
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.dynamic_modules.v3.DynamicModuleMatcher
              dynamic_module_config:
                name: {}
                do_not_close: true
              matcher_name: {}
              matcher_config:
                "@type": type.googleapis.com/google.protobuf.StringValue
                value: x-match-header
      on_match:
        action:
          name: route
          typed_config:
            "@type": type.googleapis.com/envoy.config.route.v3.Route
            match:
              prefix: /
            route:
              cluster: cluster_0
)EOF",
                                                     module_name, matcher_name);

          envoy::config::route::v3::VirtualHost virtual_host;
          TestUtility::loadFromYaml(vhost_yaml, virtual_host);
          route_config->add_virtual_hosts()->CopyFrom(virtual_host);
        });

    initialize();
  }
};

namespace {
std::vector<MatcherIntegrationParam> getMatcherTestParams() {
  std::vector<MatcherIntegrationParam> params;
  for (const auto& language : {"c", "rust", "go"}) {
    for (const auto ip : TestEnvironment::getIpVersionsForTest()) {
      params.push_back({language, ip});
    }
  }
  return params;
}

std::string matcherParamName(const testing::TestParamInfo<MatcherIntegrationParam>& info) {
  return info.param.language + "_" +
         (info.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6");
}
} // namespace

INSTANTIATE_TEST_SUITE_P(LanguagesAndIpVersions, DynamicModuleMatcherIntegrationTest,
                         testing::ValuesIn(getMatcherTestParams()), matcherParamName);

TEST_P(DynamicModuleMatcherIntegrationTest, MatchingHeaderRoutes) {
  initializeWithMatcher();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  // Request with the matching header and value should route to cluster_0.
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-match-header", "match"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(DynamicModuleMatcherIntegrationTest, NonMatchingHeaderValue) {
  initializeWithMatcher();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-match-header", "no-match"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
}

TEST_P(DynamicModuleMatcherIntegrationTest, MissingHeader) {
  initializeWithMatcher();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
}

} // namespace
} // namespace Envoy
