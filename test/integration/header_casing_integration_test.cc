#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/integration/http_integration.h"

#include "fake_upstream.h"
#include "gtest/gtest.h"

namespace Envoy {

class HeaderCasingIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  HeaderCasingIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void SetUp() override {
    setDownstreamProtocol(Http::CodecType::HTTP1);
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  void initialize() override {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_http_protocol_options()
              ->mutable_header_key_format()
              ->mutable_proper_case_words();
        });

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_explicit_http_config()
          ->mutable_http_protocol_options()
          ->mutable_header_key_format()
          ->mutable_proper_case_words();
      ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                       protocol_options);
    });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HeaderCasingIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(HeaderCasingIntegrationTest, VerifyCasedHeaders) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("http"));
  auto request = "GET / HTTP/1.1\r\nhost: host\r\nmy-header: foo\r\n\r\n";
  ASSERT_TRUE(tcp_client->write(request, false));

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  // Verify that the upstream request has proper cased headers.
  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  EXPECT_TRUE(absl::StrContains(upstream_request, "My-Header: foo"));
  EXPECT_TRUE(absl::StrContains(upstream_request, "Host: host"));

  // Verify that the downstream response has proper cased headers.
  auto response =
      "HTTP/1.1 503 Service Unavailable\r\ncontent-length: 0\r\nresponse-header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  // Verify that we're at least one proper cased header.
  tcp_client->waitForData("HTTP/1.1 503 Service Unavailable\r\nContent-Length:", true);

  tcp_client->close();
}

} // namespace Envoy
