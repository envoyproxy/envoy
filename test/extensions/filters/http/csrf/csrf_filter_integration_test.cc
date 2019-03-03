#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class CsrfFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  CsrfFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}

  void initialize() override {
    config_helper_.addFilter("name: envoy.csrf");
    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
            -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* virtual_host = route_config->mutable_virtual_hosts(0);
          {
            auto* route = virtual_host->mutable_routes(0);
            route->mutable_match()->set_prefix("/csrf-vhost-config");
            route->mutable_route()
                ->mutable_csrf()
                ->mutable_filter_enabled()
                ->mutable_default_value()
                ->set_numerator(100);
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/no-csrf");
            route->mutable_route()->set_cluster("cluster_0");
            route->mutable_route()
                ->mutable_csrf()
                ->mutable_filter_enabled()
                ->mutable_default_value()
                ->set_numerator(0);
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/csrf-route-config");
            route->mutable_route()->set_cluster("cluster_0");
            route->mutable_route()
                ->mutable_csrf()
                ->mutable_filter_enabled()
                ->mutable_default_value()
                ->set_numerator(100);
          }
        });
    HttpIntegrationTest::initialize();
  }

protected:
  void testNormalRequest(Http::TestHeaderMapImpl&& request_headers,
                         const char* expected_response_code) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, true);
    response->waitForEndStream();

    ASSERT_TRUE(response->complete());
    EXPECT_STREQ(expected_response_code, response->headers().Status()->value().c_str());
  }

  void testInvalidRequest(Http::TestHeaderMapImpl&& request_headers,
                          const char* expected_response_code) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
    response->waitForEndStream();

    ASSERT_TRUE(response->complete());
    EXPECT_STREQ(expected_response_code, response->headers().Status()->value().c_str());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CsrfFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(CsrfFilterIntegrationTest, TestVHostConfigSuccess) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "PUT"},
          {":path", "/csrf-vhost-config/test"},
          {"origin", "localhost"},
          {"host", "localhost"},
      },
      "200");
}

TEST_P(CsrfFilterIntegrationTest, TestRouteConfigSuccess) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "PUT"},
          {":path", "/csrf-route-config/test"},
          {"origin", "https://192.168.99.100:8000"},
          {"host", "192.168.99.100:8000"},
      },
      "200");
}

TEST_P(CsrfFilterIntegrationTest, TestCsrfDisabled) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "PUT"},
          {":path", "/no-csrf/test"},
          {"origin", "cross-origin"},
          {"host", "test-origin"},
      },
      "200");
}

TEST_P(CsrfFilterIntegrationTest, TestRouteConfigNonMutationMethod) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/csrf-route-config/test"},
          {"origin", "cross-origin"},
          {"host", "test-origin"},
      },
      "200");
}

TEST_P(CsrfFilterIntegrationTest, TestRouteConfigOriginMismatch) {
  testInvalidRequest(
      Http::TestHeaderMapImpl{
          {":method", "PUT"},
          {":path", "/csrf-route-config/test"},
          {"origin", "cross-origin"},
          {"host", "test-origin"},
      },
      "403");
}

TEST_P(CsrfFilterIntegrationTest, TestRouteConfigEnforcesPost) {
  testInvalidRequest(
      Http::TestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/csrf-route-config/test"},
          {"origin", "cross-origin"},
          {"host", "test-origin"},
      },
      "403");
}

TEST_P(CsrfFilterIntegrationTest, TestRouteConfigEnforcesDelete) {
  testInvalidRequest(
      Http::TestHeaderMapImpl{
          {":method", "DELETE"},
          {":path", "/csrf-route-config/test"},
          {"origin", "cross-origin"},
          {"host", "test-origin"},
      },
      "403");
}

TEST_P(CsrfFilterIntegrationTest, TestRouteConfigRefererFallback) {
  testNormalRequest(Http::TestHeaderMapImpl{{":method", "DELETE"},
                                            {":path", "/csrf-route-config/test"},
                                            {"referer", "test-origin"},
                                            {"host", "test-origin"}},
                    "200");
}

TEST_P(CsrfFilterIntegrationTest, TestRouteConfigMissingOrigin) {
  testInvalidRequest(Http::TestHeaderMapImpl{{":method", "DELETE"},
                                             {":path", "/csrf-route-config/test"},
                                             {"host", "test-origin"}},
                     "403");
}
} // namespace Envoy
