#include "envoy/config/core/v3/extension.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

class DynamicModuleFormatterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleFormatterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
  }

  void initializeWithFormatter(absl::string_view format) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);

    envoy::config::core::v3::TypedExtensionConfig formatter;
    constexpr auto formatter_config = R"EOF(
name: envoy.formatter.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.formatter.dynamic_modules.v3.DynamicModuleFormatter
  dynamic_module_config:
    name: formatter_integration_test
  formatter_name: test_formatter
)EOF";
    TestUtility::loadFromYaml(formatter_config, formatter);

    useAccessLog(format, {formatter});
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleFormatterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModuleFormatterIntegrationTest, CustomCommands) {
  initializeWithFormatter("%DYNAMIC_MODULE_REQ(x-custom)% %DYNAMIC_MODULE_RESP_CODE% "
                          "%DYNAMIC_MODULE_PROTOCOL% %DYNAMIC_MODULE_CONST% "
                          "%DYNAMIC_MODULE_REQ(x-absent)%");

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-custom", "hello"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  EXPECT_EQ("hello 200 HTTP/2 constant -", waitForAccessLog(access_log_name_));
}

TEST_P(DynamicModuleFormatterIntegrationTest, MultipleRequests) {
  initializeWithFormatter("%DYNAMIC_MODULE_CONST%");

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  for (int i = 0; i < 3; i++) {
    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {":authority", "host"}};
    auto response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("constant", waitForAccessLog(access_log_name_, i, true));
  }
}

} // namespace
} // namespace Envoy
