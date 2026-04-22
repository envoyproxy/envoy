#include "envoy/extensions/access_loggers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/http_integration.h"

namespace Envoy {

class DynamicModulesAccessLogIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModulesAccessLogIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  };

  void initializeWithAccessLogger() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
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
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesAccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesAccessLogIntegrationTest, BasicLogging) {
  initializeWithAccessLogger();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  // Verify the response was received.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  // The access logger was called. We can't easily verify this from the test since the logger
  // doesn't modify headers, but the test passing means the logger loaded and ran without crashing.
}

TEST_P(DynamicModulesAccessLogIntegrationTest, MultipleRequests) {
  initializeWithAccessLogger();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Send multiple requests to verify logging works across requests.
  for (int i = 0; i < 3; i++) {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};

    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }
}

} // namespace Envoy
