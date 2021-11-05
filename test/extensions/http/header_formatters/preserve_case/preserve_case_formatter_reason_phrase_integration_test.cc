#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"

#include "test/integration/filters/common.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace {

class PreserveCaseFormatterReasonPhraseIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  PreserveCaseFormatterReasonPhraseIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void SetUp() override {
    setDownstreamProtocol(Http::CodecType::HTTP1);
    setUpstreamProtocol(Http::CodecType::HTTP1);
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
                                       PreserveCaseFormatterConfig>("forward_reason_phrase: true");
        typed_extension_config->mutable_typed_config()->PackFrom(config);

        ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                         protocol_options);
      });
    }

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PreserveCaseFormatterReasonPhraseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(PreserveCaseFormatterReasonPhraseIntegrationTest, VerifyReasonPhrase) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  auto request = "GET / HTTP/1.1\r\nhost: host\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(request, false));

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  auto response = "HTTP/1.1 503 Slow Down\r\ncontent-length: 0\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  // Verify that the downstream response has proper reason phrase
  tcp_client->waitForData("HTTP/1.1 503 Slow Down", false);

  tcp_client->close();
}

} // namespace
} // namespace Envoy
