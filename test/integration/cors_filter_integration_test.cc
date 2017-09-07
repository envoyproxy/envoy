#include "test/integration/integration.h"
#include "test/mocks/http/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {

class CorsFilterIntegrationTest : public BaseIntegrationTest,
                                  public testing::TestWithParam<Network::Address::IpVersion> {
public:
  CorsFilterIntegrationTest() : BaseIntegrationTest(GetParam()) {}
  /**
   * Global initializer for all integration tests.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("test/config/integration/server_cors_filter.json", {"http"});
  }

  /**
   * Global destructor for all integration tests.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

protected:
  void testPreflight(Http::TestHeaderMapImpl&& request_headers,
                     Http::TestHeaderMapImpl&& response_headers) {
    executeActions({
        // [&]() -> void { response_.reset(new IntegrationStreamDecoder(*dispatcher_)); },
        [&]() -> void {
          codec_client_ = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP1);
        },
        [&]() -> void { codec_client_->makeHeaderOnlyRequest(request_headers, *response_); },
        [&]() -> void { response_->waitForEndStream(); },
        [&]() -> void { cleanupUpstreamAndDownstream(); },
    });

    EXPECT_TRUE(response_->complete());
    response_headers.iterate(
        [](const Http::HeaderEntry& entry, void* context) -> void {
          IntegrationStreamDecoder* response = static_cast<IntegrationStreamDecoder*>(context);
          Http::LowerCaseString lower_key{entry.key().c_str()};
          std::string header_value = "";
          if (response->headers().get(lower_key) != nullptr) {
            header_value = response->headers().get(lower_key)->value().c_str();
          }
          EXPECT_STREQ(entry.value().c_str(), header_value.c_str());
        },
        response_.get());
  }

  void testNormalRequest(Http::TestHeaderMapImpl&& request_headers,
                         Http::TestHeaderMapImpl&& response_headers) {
    executeActions({
        // [&]() -> void { response_.reset(new IntegrationStreamDecoder(*dispatcher_)); },
        [&]() -> void {
          codec_client_ = makeHttpConnection(lookupPort("http"), Http::CodecClient::Type::HTTP1);
        },
        [&]() -> void { sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0); },
        [&]() -> void { response_->waitForEndStream(); },
        [&]() -> void { cleanupUpstreamAndDownstream(); },
    });

    EXPECT_TRUE(response_->complete());
    response_headers.iterate(
        [](const Http::HeaderEntry& entry, void* context) -> void {
          IntegrationStreamDecoder* response = static_cast<IntegrationStreamDecoder*>(context);
          Http::LowerCaseString lower_key{entry.key().c_str()};
          std::string header_value = "";
          if (response->headers().get(lower_key) != nullptr) {
            header_value = response->headers().get(lower_key)->value().c_str();
          }
          EXPECT_STREQ(entry.value().c_str(), header_value.c_str());
        },
        response_.get());
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
          {":status", "200"},
          {"access-control-allow-origin", "test-origin"},
          {"access-control-allow-methods", "GET,POST"},
          {"access-control-allow-headers", "content-type,x-grpc-web"},
          {"access-control-expose-headers", ""},
          {"access-control-max-age", ""},
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
          {":status", "200"},
          {"access-control-allow-origin", "test-origin-1"},
          {"access-control-allow-methods", "POST"},
          {"access-control-allow-headers", "content-type"},
          {"access-control-expose-headers", "content-type"},
          {"access-control-max-age", "100"},
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
      Http::TestHeaderMapImpl{{":status", "200"}, {"access-control-allow-origin", ""}});
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
      Http::TestHeaderMapImpl{{":status", "200"}, {"access-control-allow-origin", ""}});
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
      Http::TestHeaderMapImpl{{":status", "200"}, {"access-control-allow-origin", "test-origin"}});
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
      Http::TestHeaderMapImpl{{":status", "200"},
                              {"access-control-allow-origin", "test-origin"},
                              {"access-control-allow-credentials", "true"}});
}
} // namespace Envoy
