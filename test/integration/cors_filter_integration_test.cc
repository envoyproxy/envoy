#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {

class CorsFilterIntegrationTest : public HttpIntegrationTest,
                                  public testing::TestWithParam<Network::Address::IpVersion> {
public:
  CorsFilterIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    BaseIntegrationTest::initialize();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/server_cors_filter.json", {"http"});
  }

protected:
  void testPreflight(Http::TestHeaderMapImpl&& request_headers,
                     Http::TestHeaderMapImpl&& expected_response_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    codec_client_->makeHeaderOnlyRequest(request_headers, *response_);
    response_->waitForEndStream();
    EXPECT_TRUE(response_->complete());
    compareHeaders(response_->headers(), expected_response_headers);
  }

  void testNormalRequest(Http::TestHeaderMapImpl&& request_headers,
                         Http::TestHeaderMapImpl&& expected_response_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    sendRequestAndWaitForResponse(request_headers, 0, expected_response_headers, 0);

    EXPECT_TRUE(response_->complete());
    compareHeaders(response_->headers(), expected_response_headers);
  }

  void compareHeaders(Http::TestHeaderMapImpl&& response_headers,
                      Http::TestHeaderMapImpl& expected_response_headers) {
    response_headers.remove(Envoy::Http::LowerCaseString{"date"});
    response_headers.remove(Envoy::Http::LowerCaseString{"x-envoy-upstream-service-time"});
    EXPECT_EQ(expected_response_headers, response_headers);
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, CorsFilterIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(CorsFilterIntegrationTest, TestVHostConfigSuccess) {
  testPreflight(
      Http::TestHeaderMapImpl{
          {":method", "OPTIONS"},
          {":path", "/cors-vhost-config/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"access-control-request-method", "GET"},
          {"origin", "test-origin"},
      },
      Http::TestHeaderMapImpl{
          {"access-control-allow-origin", "test-origin"},
          {"access-control-allow-methods", "GET,POST"},
          {"access-control-allow-headers", "content-type,x-grpc-web"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestRouteConfigSuccess) {
  testPreflight(
      Http::TestHeaderMapImpl{
          {":method", "OPTIONS"},
          {":path", "/cors-route-config/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"access-control-request-method", "GET"},
          {"origin", "test-origin-1"},
      },
      Http::TestHeaderMapImpl{
          {"access-control-allow-origin", "test-origin-1"},
          {"access-control-allow-methods", "POST"},
          {"access-control-allow-headers", "content-type"},
          {"access-control-expose-headers", "content-type"},
          {"access-control-max-age", "100"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestRouteConfigBadOrigin) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "OPTIONS"},
          {":path", "/cors-route-config/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"access-control-request-method", "GET"},
          {"origin", "test-origin"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestCorsDisabled) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "OPTIONS"},
          {":path", "/no-cors/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"access-control-request-method", "GET"},
          {"origin", "test-origin"},
      },
      Http::TestHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestEncodeHeaders) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/cors-vhost-config/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"origin", "test-origin"},
      },
      Http::TestHeaderMapImpl{
          {"access-control-allow-origin", "test-origin"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestEncodeHeadersCredentialsAllowed) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/cors-credentials-allowed/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"origin", "test-origin"},
      },
      Http::TestHeaderMapImpl{
          {"access-control-allow-origin", "test-origin"},
          {"access-control-allow-credentials", "true"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}
} // namespace Envoy
