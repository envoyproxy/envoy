#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {
namespace {

const std::string CORS_CONFIG_YAML = R"EOF(
name: test-cors-host
virtual_hosts:
- name: test-host
  domains: ["*"]
  typed_per_filter_config:
    envoy.filters.http.cors:
      "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.CorsPolicy
      allow_origin_string_match:
      - safe_regex:
          regex: ".*"
      allow_headers: "content-type,x-grpc-web"
      allow_methods: "GET,POST"
  routes:
  - match:
      prefix: "/cors-vhost-config"
    route:
      cluster: "cluster_0"
  - match:
      prefix: "/no-cors"
    route:
      cluster: "cluster_0"
    typed_per_filter_config:
      envoy.filters.http.cors:
        "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.CorsPolicy
        filter_enabled:
          default_value:
            numerator: 0
  - match:
      prefix: "/cors-route-config"
    route:
      cluster: "cluster_0"
    typed_per_filter_config:
      envoy.filters.http.cors:
        "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.CorsPolicy
        allow_origin_string_match:
        - exact: test-origin-1
        - exact: test-host-2
        allow_headers: content-type
        allow_methods: POST
        max_age: "100"
  - match:
      prefix: "/cors-credentials-allowed"
    route:
      cluster: "cluster_0"
    typed_per_filter_config:
      envoy.filters.http.cors:
        "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.CorsPolicy
        allow_origin_string_match:
        - exact: test-origin-1
        allow_credentials: true
  - match:
      prefix: "/cors-allow-origin-regex"
    route:
      cluster: "cluster_0"
    typed_per_filter_config:
      envoy.filters.http.cors:
        "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.CorsPolicy
        allow_origin_string_match:
        - safe_regex:
            regex: ".*\\.envoyproxy\\.io"
        forward_not_matching_preflights: false
  - match:
      prefix: "/cors-expose-headers"
    route:
      cluster: "cluster_0"
    typed_per_filter_config:
      envoy.filters.http.cors:
        "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.CorsPolicy
        allow_origin_string_match:
        - exact: test-origin-1
        expose_headers: "custom-header-1,custom-header-2"
)EOF";

struct TestParam {
  Network::Address::IpVersion ip_version{};
  bool cors_policy_from_per_filter_config{};
};

std::vector<TestParam> testParams() {
  std::vector<TestParam> params;
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    params.push_back({ip_version, true});

    // Ignore the 'cors' field tests if the deprecated feature is disabled by compile time flag.
#ifndef ENVOY_DISABLE_DEPRECATED_FEATURES
    params.push_back({ip_version, false});
#endif
  }
  return params;
}

std::string testParamsToString(const ::testing::TestParamInfo<TestParam>& params) {
  std::string param_string;
  if (params.param.ip_version == Network::Address::IpVersion::v4) {
    param_string.append("IPv4");
  } else {
    param_string.append("IPv6");
  }

  if (params.param.cors_policy_from_per_filter_config) {
    param_string.append("_cors_policy_from_per_filter_config");
  } else {
    param_string.append("_cors_policy_from_route_vh_cors_field");
  }
  return param_string;
}

class CorsFilterIntegrationTest : public testing::TestWithParam<TestParam>,
                                  public Envoy::HttpIntegrationTest {
public:
  CorsFilterIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().ip_version) {}

  void initialize() override {
    config_helper_.prependFilter("name: envoy.filters.http.cors");
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          if (GetParam().cors_policy_from_per_filter_config) {
            route_config->Clear();
            TestUtility::loadFromYaml(CORS_CONFIG_YAML, *route_config);
            return;
          }

          auto* virtual_host = route_config->mutable_virtual_hosts(0);
          {
            auto* cors = virtual_host->mutable_cors();
            auto* regex = cors->add_allow_origin_string_match()->mutable_safe_regex();
            regex->set_regex(".*");
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
            cors->add_allow_origin_string_match()->set_exact("test-origin-1");
            cors->add_allow_origin_string_match()->set_exact("test-host-2");
            cors->set_allow_headers("content-type");
            cors->set_allow_methods("POST");
            cors->set_max_age("100");
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/cors-credentials-allowed");
            route->mutable_route()->set_cluster("cluster_0");
            auto* cors = route->mutable_route()->mutable_cors();
            cors->add_allow_origin_string_match()->set_exact("test-origin-1");
            cors->mutable_allow_credentials()->set_value(true);
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/cors-allow-origin-regex");
            route->mutable_route()->set_cluster("cluster_0");
            auto* cors = route->mutable_route()->mutable_cors();
            auto* safe_regex =
                cors->mutable_allow_origin_string_match()->Add()->mutable_safe_regex();
            safe_regex->set_regex(".*\\.envoyproxy\\.io");
          }

          {
            auto* route = virtual_host->add_routes();
            route->mutable_match()->set_prefix("/cors-expose-headers");
            route->mutable_route()->set_cluster("cluster_0");
            auto* cors = route->mutable_route()->mutable_cors();
            cors->add_allow_origin_string_match()->set_exact("test-origin-1");
            cors->set_expose_headers("custom-header-1,custom-header-2");
          }
        });
    HttpIntegrationTest::initialize();
  }

protected:
  void testPreflight(Http::TestRequestHeaderMapImpl&& request_headers,
                     Http::TestResponseHeaderMapImpl&& expected_response_headers) {
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
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

INSTANTIATE_TEST_SUITE_P(IpVersionsAndConfig, CorsFilterIntegrationTest,
                         testing::ValuesIn(testParams()), testParamsToString);

TEST_P(CorsFilterIntegrationTest, TestVHostConfigSuccess) {
  testPreflight(
      Http::TestRequestHeaderMapImpl{
          {":method", "OPTIONS"},
          {":path", "/cors-vhost-config/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"access-control-request-method", "GET"},
          {"origin", "test-origin"},
      },
      Http::TestResponseHeaderMapImpl{
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
      Http::TestRequestHeaderMapImpl{
          {":method", "OPTIONS"},
          {":path", "/cors-route-config/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"access-control-request-method", "GET"},
          {"origin", "test-origin-1"},
      },
      Http::TestResponseHeaderMapImpl{
          {"access-control-allow-origin", "test-origin-1"},
          {"access-control-allow-methods", "POST"},
          {"access-control-allow-headers", "content-type"},
          {"access-control-max-age", "100"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestRouteConfigBadOrigin) {
  testNormalRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "OPTIONS"},
          {":path", "/cors-route-config/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"access-control-request-method", "GET"},
          {"origin", "test-origin"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestCorsDisabled) {
  testNormalRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "OPTIONS"},
          {":path", "/no-cors/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"access-control-request-method", "GET"},
          {"origin", "test-origin"},
      },
      Http::TestResponseHeaderMapImpl{
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestEncodeHeaders) {
  testNormalRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/cors-vhost-config/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"origin", "test-origin"},
      },
      Http::TestResponseHeaderMapImpl{
          {"access-control-allow-origin", "test-origin"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestEncodeHeadersCredentialsAllowed) {
  testNormalRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/cors-credentials-allowed/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"origin", "test-origin"},
      },
      Http::TestResponseHeaderMapImpl{
          {"access-control-allow-origin", "test-origin"},
          {"access-control-allow-credentials", "true"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestAllowedOriginRegex) {
  testNormalRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/cors-allow-origin-regex/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"origin", "www.envoyproxy.io"},
      },
      Http::TestResponseHeaderMapImpl{
          {"access-control-allow-origin", "www.envoyproxy.io"},
          {"access-control-allow-credentials", "true"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestNotAllowedOriginRegex) {
  testNormalRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/cors-allow-origin-regex/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"origin", "www.envooooooyproxy.io"},
      },
      Http::TestResponseHeaderMapImpl{
          {"access-control-allow-credentials", "true"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

TEST_P(CorsFilterIntegrationTest, TestExposeHeaders) {
  testNormalRequest(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"},
          {":path", "/cors-expose-headers/test"},
          {":scheme", "http"},
          {":authority", "test-host"},
          {"origin", "test-origin-1"},
      },
      Http::TestResponseHeaderMapImpl{
          {"access-control-allow-origin", "test-origin-1"},
          {"access-control-expose-headers", "custom-header-1,custom-header-2"},
          {"server", "envoy"},
          {"content-length", "0"},
          {":status", "200"},
      });
}

} // namespace
} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
