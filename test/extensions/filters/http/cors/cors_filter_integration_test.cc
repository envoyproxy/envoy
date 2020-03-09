#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class CorsFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  CorsFilterIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addFilter("name: envoy.filters.http.cors");
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* virtual_host = route_config->mutable_virtual_hosts(0);
          {
            auto* cors = virtual_host->mutable_cors();
            cors->add_hidden_envoy_deprecated_allow_origin("*");
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
            route->mutable_route()
                ->mutable_cors()
                ->mutable_filter_enabled()
                ->mutable_default_value()
                ->set_numerator(0);
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/cors-route-config");
            route->mutable_route()->set_cluster("cluster_0");
            auto* cors = route->mutable_route()->mutable_cors();
            cors->add_hidden_envoy_deprecated_allow_origin("test-origin-1");
            cors->add_hidden_envoy_deprecated_allow_origin("test-host-2");
            cors->set_allow_headers("content-type");
            cors->set_allow_methods("POST");
            cors->set_max_age("100");
          }

          {
            // TODO(mattklein123): When deprecated config is removed, remove DEPRECATED_FEATURE_TEST
            // from all tests below.
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/cors-credentials-allowed");
            route->mutable_route()->set_cluster("cluster_0");
            auto* cors = route->mutable_route()->mutable_cors();
            cors->add_hidden_envoy_deprecated_allow_origin("test-origin-1");
            cors->mutable_allow_credentials()->set_value(true);
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/cors-allow-origin-regex");
            route->mutable_route()->set_cluster("cluster_0");
            auto* cors = route->mutable_route()->mutable_cors();
            auto* safe_regex =
                cors->mutable_allow_origin_string_match()->Add()->mutable_safe_regex();
            safe_regex->mutable_google_re2();
            safe_regex->set_regex(".*\\.envoyproxy\\.io");
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/cors-expose-headers");
            route->mutable_route()->set_cluster("cluster_0");
            auto* cors = route->mutable_route()->mutable_cors();
            cors->add_hidden_envoy_deprecated_allow_origin("test-origin-1");
            cors->set_expose_headers("custom-header-1,custom-header-2");
          }
        });
    config_helper_.addRuntimeOverride("envoy.deprecated_features:envoy.config.route.v3.CorsPolicy."
                                      "hidden_envoy_deprecated_allow_origin",
                                      "true");
    HttpIntegrationTest::initialize();
  }

protected:
  void testPreflight(Http::TestRequestHeaderMapImpl&& request_headers,
                     Http::TestResponseHeaderMapImpl&& expected_response_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    response->waitForEndStream();
    EXPECT_TRUE(response->complete());
    compareHeaders(response->headers(), expected_response_headers);
  }

  void testNormalRequest(Http::TestRequestHeaderMapImpl&& request_headers,
                         Http::TestResponseHeaderMapImpl&& expected_response_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = sendRequestAndWaitForResponse(request_headers, 0, expected_response_headers, 0);

    EXPECT_TRUE(response->complete());
    compareHeaders(response->headers(), expected_response_headers);
  }

  void compareHeaders(Http::TestResponseHeaderMapImpl&& response_headers,
                      Http::TestResponseHeaderMapImpl& expected_response_headers) {
    response_headers.remove(Envoy::Http::LowerCaseString{"date"});
    response_headers.remove(Envoy::Http::LowerCaseString{"x-envoy-upstream-service-time"});
    EXPECT_EQ(expected_response_headers, response_headers);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CorsFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(CorsFilterIntegrationTest, DEPRECATED_FEATURE_TEST(TestVHostConfigSuccess)) {
  config_helper_.addRuntimeOverride("envoy.deprecated_features:envoy.config.route.v3."
                                    "CorsPolicy.hidden_envoy_deprecated_enabled",
                                    "true");
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

TEST_P(CorsFilterIntegrationTest, DEPRECATED_FEATURE_TEST(TestRouteConfigSuccess)) {
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
          {"access-control-max-age", "100"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, DEPRECATED_FEATURE_TEST(TestRouteConfigBadOrigin)) {
  config_helper_.addRuntimeOverride("envoy.deprecated_features:envoy.config.route.v3."
                                    "CorsPolicy.hidden_envoy_deprecated_enabled",
                                    "true");
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

TEST_P(CorsFilterIntegrationTest, DEPRECATED_FEATURE_TEST(TestCorsDisabled)) {
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

TEST_P(CorsFilterIntegrationTest, DEPRECATED_FEATURE_TEST(TestLegacyCorsDisabled)) {
  config_helper_.addRuntimeOverride("envoy.deprecated_features:envoy.config.route.v3."
                                    "CorsPolicy.hidden_envoy_deprecated_enabled",
                                    "true");

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(0);
        auto* route = virtual_host->add_routes();
        route->mutable_match()->set_prefix("/legacy-no-cors");
        route->mutable_route()->set_cluster("cluster_0");
        route->mutable_route()
            ->mutable_cors()
            ->mutable_hidden_envoy_deprecated_enabled()
            ->set_value(false);
      });
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "OPTIONS"},
          {":path", "/legacy-no-cors/test"},
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

TEST_P(CorsFilterIntegrationTest, DEPRECATED_FEATURE_TEST(TestEncodeHeaders)) {
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

TEST_P(CorsFilterIntegrationTest, DEPRECATED_FEATURE_TEST(TestEncodeHeadersCredentialsAllowed)) {
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

TEST_P(CorsFilterIntegrationTest, DEPRECATED_FEATURE_TEST(TestAllowedOriginRegex)) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/cors-allow-origin-regex/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"origin", "www.envoyproxy.io"},
      },
      Http::TestHeaderMapImpl{
          {"access-control-allow-origin", "www.envoyproxy.io"},
          {"access-control-allow-credentials", "true"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, DEPRECATED_FEATURE_TEST(TestExposeHeaders)) {
  testNormalRequest(
      Http::TestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/cors-expose-headers/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"origin", "test-origin-1"},
      },
      Http::TestHeaderMapImpl{
          {"access-control-allow-origin", "test-origin-1"},
          {"access-control-expose-headers", "custom-header-1,custom-header-2"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}
} // namespace Envoy
