#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"

#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace {

// See https://github.com/envoyproxy/envoy/issues/21245.
enum class ParserImpl {
  HttpParser, // http-parser from node.js
  BalsaParser // Balsa from QUICHE
};

struct TestParams {
  Network::Address::IpVersion ip_version;
  ParserImpl parser_impl;
  bool forward_reason_phrase;
};

std::string testParamsToString(const ::testing::TestParamInfo<TestParams>& p) {
  return fmt::format("{}_{}_{}",
                     p.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6",
                     p.param.parser_impl == ParserImpl::HttpParser ? "HttpParser" : "BalsaParser",
                     p.param.forward_reason_phrase ? "enabled" : "disabled");
}

std::vector<TestParams> getTestsParams() {
  std::vector<TestParams> ret;

  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    for (auto parser_impl : {ParserImpl::HttpParser, ParserImpl::BalsaParser}) {
      ret.push_back(TestParams{ip_version, parser_impl, true});
      ret.push_back(TestParams{ip_version, parser_impl, false});
    }
  }

  return ret;
}

class PreserveCaseFormatterReasonPhraseIntegrationTest : public testing::TestWithParam<TestParams>,
                                                         public HttpIntegrationTest {
public:
  PreserveCaseFormatterReasonPhraseIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().ip_version) {}

  void SetUp() override {
    setDownstreamProtocol(Http::CodecType::HTTP1);
    setUpstreamProtocol(Http::CodecType::HTTP1);
    if (GetParam().parser_impl == ParserImpl::BalsaParser) {
      scoped_runtime_.mergeValues({{"envoy.reloadable_features.http1_use_balsa_parser", "true"}});
    } else {
      scoped_runtime_.mergeValues({{"envoy.reloadable_features.http1_use_balsa_parser", "false"}});
    }
  }

  void initialize() override {
    if (upstreamProtocol() == Http::CodecType::HTTP1) {
      config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        ConfigHelper::HttpProtocolOptions protocol_options;
        auto typed_extension_config = protocol_options.mutable_explicit_http_config()
                                          ->mutable_http_protocol_options()
                                          ->mutable_header_key_format()
                                          ->mutable_stateful_formatter();
        typed_extension_config->set_name("preserve_case");

        auto config =
            TestUtility::parseYaml<envoy::extensions::http::header_formatters::preserve_case::v3::
                                       PreserveCaseFormatterConfig>(fmt::format(
                "forward_reason_phrase: {}", GetParam().forward_reason_phrase ? "true" : "false"));
        typed_extension_config->mutable_typed_config()->PackFrom(config);

        ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                         protocol_options);
      });
    }

    HttpIntegrationTest::initialize();
  }

private:
  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(CaseFormatter, PreserveCaseFormatterReasonPhraseIntegrationTest,
                         testing::ValuesIn(getTestsParams()), testParamsToString);

TEST_P(PreserveCaseFormatterReasonPhraseIntegrationTest, VerifyReasonPhraseEnabled) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  auto request = "GET / HTTP/1.1\r\nhost: host\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(request, false));

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  auto response = "HTTP/1.1 503 Slow Down\r\ncontent-length: 0\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  auto expected_reason_phrase =
      GetParam().forward_reason_phrase ? "Slow Down" : "Service Unavailable";
  // Verify that the downstream response has proper reason phrase
  tcp_client->waitForData(fmt::format("HTTP/1.1 503 {}", expected_reason_phrase), false);

  tcp_client->close();
}

} // namespace
} // namespace Envoy
