#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

struct FormatterOnEnvoyHeadersTestParams {
  Network::Address::IpVersion ip_version;
  envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      FormatterTypeOnEnvoyHeaders formatter_type_on_envoy_headers;
};

std::string formatterOnEnvoyHeadersTestParamsToString(
    const ::testing::TestParamInfo<FormatterOnEnvoyHeadersTestParams>& p) {
  return fmt::format("{}_{}", TestUtility::ipVersionToString(p.param.ip_version),
                     static_cast<int>(p.param.formatter_type_on_envoy_headers));
}

std::vector<FormatterOnEnvoyHeadersTestParams> getFormatterOnEnvoyHeadersTestParams() {
  std::vector<FormatterOnEnvoyHeadersTestParams> ret;

  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    ret.push_back(FormatterOnEnvoyHeadersTestParams{
        ip_version, envoy::extensions::http::header_formatters::preserve_case::v3::
                        PreserveCaseFormatterConfig::DEFAULT});
    ret.push_back(FormatterOnEnvoyHeadersTestParams{
        ip_version, envoy::extensions::http::header_formatters::preserve_case::v3::
                        PreserveCaseFormatterConfig::PROPER_CASE});
  }

  return ret;
}

// Demonstrate using a filter to affect the case.
class PreserveCaseFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "preserve-case-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    headers.addCopy(Http::LowerCaseString("request-header"), "request-header-value");
    headers.formatter()->processKey("Request-Header");

    headers.addCopy(Http::LowerCaseString("x-forwarded-for"), "x-forwarded-for-value");

    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    headers.addCopy(Http::LowerCaseString("response-header"), "response-header-value");
    headers.formatter()->processKey("Response-Header");

    headers.addCopy(Http::LowerCaseString("hello-header"), "hello-header-value");

    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char PreserveCaseFilter::name[];

class PreserveCaseIntegrationTest
    : public testing::TestWithParam<FormatterOnEnvoyHeadersTestParams>,
      public HttpIntegrationTest {
public:
  PreserveCaseIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().ip_version),
        registration_(factory_) {}

  void initialize() override {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto typed_extension_config = hcm.mutable_http_protocol_options()
                                            ->mutable_header_key_format()
                                            ->mutable_stateful_formatter();
          typed_extension_config->set_name("preserve_case");
          auto config = TestUtility::parseYaml<envoy::extensions::http::header_formatters::
                                                   preserve_case::v3::PreserveCaseFormatterConfig>(
              fmt::format("formatter_type_on_envoy_headers: {}",
                          static_cast<int>(GetParam().formatter_type_on_envoy_headers)));
          typed_extension_config->mutable_typed_config()->PackFrom(config);
        });

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ConfigHelper::HttpProtocolOptions protocol_options;
      auto typed_extension_config = protocol_options.mutable_explicit_http_config()
                                        ->mutable_http_protocol_options()
                                        ->mutable_header_key_format()
                                        ->mutable_stateful_formatter();
      typed_extension_config->set_name("preserve_case");
      auto config = TestUtility::parseYaml<envoy::extensions::http::header_formatters::
                                               preserve_case::v3::PreserveCaseFormatterConfig>(
          fmt::format("formatter_type_on_envoy_headers: {}",
                      static_cast<int>(GetParam().formatter_type_on_envoy_headers)));
      typed_extension_config->mutable_typed_config()->PackFrom(config);
      ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                       protocol_options);
    });

    HttpIntegrationTest::initialize();
  }

  SimpleFilterConfig<PreserveCaseFilter> factory_;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registration_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PreserveCaseIntegrationTest,
                         testing::ValuesIn(getFormatterOnEnvoyHeadersTestParams()),
                         formatterOnEnvoyHeadersTestParamsToString);

// Verify that we preserve case in both directions.
TEST_P(PreserveCaseIntegrationTest, EndToEnd) {
  config_helper_.prependFilter(R"EOF(
  name: preserve-case-filter
  )EOF");
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  auto request = "GET / HTTP/1.1\r\nHOst: host\r\nMy-Request-Header: foo\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(request, false));

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  // Verify that the upstream request has preserved cased headers.
  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  EXPECT_TRUE(absl::StrContains(upstream_request, "My-Request-Header: foo"));
  EXPECT_TRUE(absl::StrContains(upstream_request, "HOst: host"));
  EXPECT_TRUE(absl::StrContains(upstream_request, "Request-Header: request-header-value"));
  switch (GetParam().formatter_type_on_envoy_headers) {
  case envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      DEFAULT:
    EXPECT_TRUE(absl::StrContains(upstream_request, "x-forwarded-for: x-forwarded-for-value"));
    break;
  case envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      PROPER_CASE:
    EXPECT_TRUE(absl::StrContains(upstream_request, "X-Forwarded-For: x-forwarded-for-value"));
    break;
  default:
    EXPECT_TRUE(absl::StrContains(upstream_request, "x-forwarded-for: x-forwarded-for-value"));
    break;
  }

  // Verify that the downstream response has preserved cased headers.
  auto response =
      "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nMy-Response-Header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  // Verify that downstream response has preserved case headers.
  tcp_client->waitForData("Content-Length: 0", false);
  tcp_client->waitForData("My-Response-Header: foo", false);
  tcp_client->waitForData("Response-Header: response-header-value", false);
  switch (GetParam().formatter_type_on_envoy_headers) {
  case envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      DEFAULT:
    tcp_client->waitForData("hello-header: hello-header-value", false);
    break;
  case envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
      PROPER_CASE:
    tcp_client->waitForData("Hello-Header: hello-header-value", false);
    break;
  default:
    tcp_client->waitForData("hello-header: hello-header-value", false);
    break;
  }

  tcp_client->close();
}

} // namespace
} // namespace Envoy
