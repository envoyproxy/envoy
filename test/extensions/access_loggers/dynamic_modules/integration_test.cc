#include "envoy/extensions/access_loggers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/http_integration.h"

namespace Envoy {

// Parameterized over (language, IP version). language selects which test_data subdirectory
// (rust, go) the access logger module is loaded from. Both languages ship a module named
// "access_log_integration_test" exposing a "test_logger" access logger that exercises the
// full AccessLogContext getter surface and records select getter results into per-config
// counters/gauges so this driver can verify correctness via /stats.
struct AccessLogParam {
  std::string language;
  Network::Address::IpVersion ip_version;
};

class DynamicModulesAccessLogIntegrationTest : public testing::TestWithParam<AccessLogParam>,
                                               public HttpIntegrationTest {
public:
  DynamicModulesAccessLogIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam().ip_version) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  };

  void initializeWithAccessLogger() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam().language),
        1);

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          constexpr auto config = R"EOF(
name: envoy.access_loggers.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.dynamic_modules.v3.DynamicModuleAccessLog
  dynamic_module_config:
    name: access_log_integration_test
  logger_name: test_logger
  logger_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: test_config
)EOF";
          envoy::config::accesslog::v3::AccessLog access_log;
          TestUtility::loadFromYaml(config, access_log);
          hcm.add_access_log()->CopyFrom(access_log);
        });

    initialize();
  }

  // Stats from the access logger are scoped under the default metrics namespace
  // "dynamicmodulescustom.<name>" — see access_loggers/dynamic_modules/config.cc.
  uint64_t counter(absl::string_view name) {
    return test_server_->counter(absl::StrCat("dynamicmodulescustom.", name))->value();
  }

  uint64_t gauge(absl::string_view name) {
    return test_server_->gauge(absl::StrCat("dynamicmodulescustom.", name))->value();
  }
};

namespace {
std::vector<AccessLogParam> getAccessLogTestParams() {
  std::vector<AccessLogParam> params;
  for (const auto& language : {"rust", "go"}) {
    for (const auto ip : TestEnvironment::getIpVersionsForTest()) {
      params.push_back({language, ip});
    }
  }
  return params;
}

std::string accessLogParamName(const testing::TestParamInfo<AccessLogParam>& info) {
  return info.param.language + "_" +
         (info.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6");
}
} // namespace

INSTANTIATE_TEST_SUITE_P(SdkLanguagesAndIpVersions, DynamicModulesAccessLogIntegrationTest,
                         testing::ValuesIn(getAccessLogTestParams()), accessLogParamName);

TEST_P(DynamicModulesAccessLogIntegrationTest, BasicLogging) {
  initializeWithAccessLogger();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  // The Go and Rust modules increment counters and set gauges from data they read off
  // the AccessLogContext — assert those values match the wire data we sent above.
  test_server_->waitForCounterEq("dynamicmodulescustom.test_log_count", 1);

  // Specific getters: response code, method, path, request protocol.
  EXPECT_EQ(1, counter("test_response_code_200"));
  EXPECT_EQ(1, counter("test_method_get"));
  EXPECT_EQ(1, counter("test_path_test"));
  EXPECT_EQ(1, counter("test_request_protocol_http2"));

  // Gauges hold the last observed values.
  EXPECT_EQ(200, gauge("test_response_code_last"));
  // The 4-header request (method, path, scheme, authority) plus internal envoy headers
  // means request_headers_count must be at least 4. The exact value depends on Envoy
  // header insertions so we use a lower bound.
  EXPECT_GE(gauge("test_request_headers_count"), 4u);
}

TEST_P(DynamicModulesAccessLogIntegrationTest, MultipleRequests) {
  initializeWithAccessLogger();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  for (int i = 0; i < 3; i++) {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }

  // Counters accumulate across requests — assert all three were observed.
  test_server_->waitForCounterEq("dynamicmodulescustom.test_log_count", 3);
  EXPECT_EQ(3, counter("test_response_code_200"));
  EXPECT_EQ(3, counter("test_method_get"));
  EXPECT_EQ(3, counter("test_path_test"));
}

// Verify a non-matching request increments the log_count counter but NOT the
// header-specific counters (i.e., the SDK getters didn't fabricate matches that aren't
// there).
TEST_P(DynamicModulesAccessLogIntegrationTest, GetterValuesMatchActualRequest) {
  initializeWithAccessLogger();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Send a POST to a different path so :method != GET and :path != /test.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/other"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  test_server_->waitForCounterEq("dynamicmodulescustom.test_log_count", 1);
  // :method was POST, not GET — counter should remain 0.
  EXPECT_EQ(0, counter("test_method_get"));
  // :path was /other, not /test — counter should remain 0.
  EXPECT_EQ(0, counter("test_path_test"));
  // Response was still 200, so this counter does increment.
  EXPECT_EQ(1, counter("test_response_code_200"));
}

} // namespace Envoy
