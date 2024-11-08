#include <string>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/api_key_auth/v3/api_key_auth.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {
namespace {

const std::string ApiKeyAuthFilterConfig =
    R"EOF(
name: envoy.filters.http.api_key_auth
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.api_key_auth.v3.ApiKeyAuth
  credentials:
    entries:
      - key: key1
        client_id: user1
      - key: key2
        client_id: user2
  key_sources:
    entries:
      - header: "Authorization"
)EOF";

const std::string ApiKeyAuthScopeConfig =
    R"EOF(
allowed_clients:
  - user1
)EOF";

class ApiKeyAuthIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initializeFilter() {
    config_helper_.prependFilter(ApiKeyAuthFilterConfig);
    initialize();
  }

  void initializeWithPerRouteConfig() {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               cfg) {
          envoy::extensions::filters::http::api_key_auth::v3::ApiKeyAuthPerRoute per_route_config;
          TestUtility::loadFromYaml(ApiKeyAuthScopeConfig, per_route_config);

          auto* config = cfg.mutable_route_config()
                             ->mutable_virtual_hosts()
                             ->Mutable(0)
                             ->mutable_typed_per_filter_config();

          (*config)["envoy.filters.http.api_key_auth"].PackFrom(per_route_config);
        });
    config_helper_.prependFilter(ApiKeyAuthFilterConfig);
    initialize();
  }
};

class ApiKeyAuthIntegrationTestAllProtocols : public ApiKeyAuthIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, ApiKeyAuthIntegrationTestAllProtocols,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Request with valid credential
TEST_P(ApiKeyAuthIntegrationTestAllProtocols, ValidCredential) {
  initializeFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  {

    auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
        {":method", "GET"},
        {":path", "/"},
        {":scheme", "http"},
        {":authority", "host"},
        {"Authorization", "Bearer key1"},
    });

    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  {
    auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
        {":method", "GET"},
        {":path", "/"},
        {":scheme", "http"},
        {":authority", "host"},
        {"Authorization", "Bearer key2"},
    });

    waitForNextUpstreamRequest();

    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
}

TEST_P(ApiKeyAuthIntegrationTestAllProtocols, NoCredential) {
  initializeFilter();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ("Client authentication failed.", response->body());
}

TEST_P(ApiKeyAuthIntegrationTestAllProtocols, InvalidCredentical) {
  initializeFilter();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer key3"},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ("Client authentication failed.", response->body());
}

TEST_P(ApiKeyAuthIntegrationTestAllProtocols, NotAllowdClient) {
  initializeWithPerRouteConfig();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Bearer key2"},
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_EQ("Client is forbidden.", response->body());
}

} // namespace
} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
