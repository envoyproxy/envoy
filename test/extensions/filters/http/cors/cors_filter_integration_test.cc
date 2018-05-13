#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class CorsFilterIntegrationTest : public HttpIntegrationTest,
                                  public testing::TestWithParam<Network::Address::IpVersion> {
public:
  CorsFilterIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addFilter("name: envoy.cors");
    config_helper_.addConfigModifier(
        [&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
            -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* virtual_host = route_config->mutable_virtual_hosts(0);
          {
            auto* cors = virtual_host->mutable_cors();
            cors->add_allow_origin("*");
            cors->set_allow_headers("content-type,x-grpc-web");
            cors->set_allow_methods("GET,POST");
          }

          {
            auto* route = virtual_host->mutable_routes(0);
            route->mutable_match()->set_prefix("/cors-vhost-config");
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/no-cors");
            route->mutable_route()->set_cluster("cluster_0");
            route->mutable_route()->mutable_cors()->mutable_enabled()->set_value(false);
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/cors-route-config");
            route->mutable_route()->set_cluster("cluster_0");
            auto* cors = route->mutable_route()->mutable_cors();
            cors->add_allow_origin("test-origin-1");
            cors->add_allow_origin("test-host-2");
            cors->set_allow_headers("content-type");
            cors->set_allow_methods("POST");
            cors->set_expose_headers("content-type");
            cors->set_max_age("100");
          }
          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/cors-credentials-allowed");
            route->mutable_route()->set_cluster("cluster_0");
            auto* cors = route->mutable_route()->mutable_cors();
            cors->add_allow_origin("test-origin-1");
            cors->mutable_allow_credentials()->set_value(true);
          }
        });
    HttpIntegrationTest::initialize();
  }

protected:
  void testPreflight(Http::TestHeaderMapImpl&& request_headers,
                     Http::TestHeaderMapImpl&& expected_response_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    response->waitForEndStream();
    EXPECT_TRUE(response->complete());
    compareHeaders(response->headers(), expected_response_headers);
  }

  void testNormalRequest(Http::TestHeaderMapImpl&& request_headers,
                         Http::TestHeaderMapImpl&& expected_response_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = sendRequestAndWaitForResponse(request_headers, 0, expected_response_headers, 0);

    EXPECT_TRUE(response->complete());
    compareHeaders(response->headers(), expected_response_headers);
  }

  void compareHeaders(Http::TestHeaderMapImpl&& response_headers,
                      Http::TestHeaderMapImpl& expected_response_headers) {
    response_headers.remove(Envoy::Http::LowerCaseString{"date"});
    response_headers.remove(Envoy::Http::LowerCaseString{"x-envoy-upstream-service-time"});
    EXPECT_EQ(expected_response_headers, response_headers);
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, CorsFilterIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

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
