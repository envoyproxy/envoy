#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

class PreserveCaseIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  PreserveCaseIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::extensions::filters::network::
                                            http_connection_manager::v3::HttpConnectionManager&
                                                hcm) {
      auto typed_extension_config = hcm.mutable_http_protocol_options()
                                        ->mutable_header_key_format()
                                        ->mutable_stateful_formatter();
      typed_extension_config->set_name("preserve_case");
      typed_extension_config->mutable_typed_config()->set_type_url(
          "type.googleapis.com/"
          "envoy.extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig");
    });

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ConfigHelper::HttpProtocolOptions protocol_options;
      auto typed_extension_config = protocol_options.mutable_explicit_http_config()
                                        ->mutable_http_protocol_options()
                                        ->mutable_header_key_format()
                                        ->mutable_stateful_formatter();
      typed_extension_config->set_name("preserve_case");
      typed_extension_config->mutable_typed_config()->set_type_url(
          "type.googleapis.com/"
          "envoy.extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig");
      ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                       protocol_options);
    });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, PreserveCaseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that we preserve case in both directions.
TEST_P(PreserveCaseIntegrationTest, EndToEnd) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  auto request = "GET / HTTP/1.1\r\nHOst: host\r\nMy-Header: foo\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(request, false));

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  // Verify that the upstream request has preserved cased headers.
  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  EXPECT_TRUE(absl::StrContains(upstream_request, "My-Header: foo"));
  EXPECT_TRUE(absl::StrContains(upstream_request, "HOst: host"));

  // Verify that the downstream response has proper cased headers.
  auto response =
      "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nResponse-Header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  // Verify that downstream response has preserved case headers.
  std::string downstream_response;
  tcp_client->waitForData("Content-Length: 0", false);
  tcp_client->waitForData("Response-Header: foo", false);
  tcp_client->close();
}

} // namespace
} // namespace Envoy