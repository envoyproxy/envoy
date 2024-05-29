#include <string>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/basic_auth/v3/basic_auth.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {
namespace {

// user1, test1
// user2, test2
const std::string BasicAuthFilterConfig =
    R"EOF(
name: envoy.filters.http.basic_auth
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.basic_auth.v3.BasicAuth
  users:
    inline_string: |-
      user1:{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
      user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  forward_username_header: x-username
)EOF";

// admin, admin
const std::string AdminUsers =
    R"EOF(
users:
  inline_string: |-
    admin:{SHA}0DPiKuNIrrVmD8IUCuw1hQxNqZc=
)EOF";

class BasicAuthIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initializeFilter() {
    config_helper_.prependFilter(BasicAuthFilterConfig);
    initialize();
  }

  void initializePerRouteFilter(const std::string& yaml_config) {
    config_helper_.addConfigModifier(
        [&yaml_config](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                cfg) {
          envoy::extensions::filters::http::basic_auth::v3::BasicAuthPerRoute per_route_config;
          TestUtility::loadFromYaml(yaml_config, per_route_config);

          auto* config = cfg.mutable_route_config()
                             ->mutable_virtual_hosts()
                             ->Mutable(0)
                             ->mutable_typed_per_filter_config();

          (*config)["envoy.filters.http.basic_auth"].PackFrom(per_route_config);
        });
    config_helper_.prependFilter(BasicAuthFilterConfig);
    initialize();
  }

  void disablePerRouteFilter() {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               cfg) {
          envoy::config::route::v3::FilterConfig per_route_config;
          per_route_config.set_disabled(true);

          auto* config = cfg.mutable_route_config()
                             ->mutable_virtual_hosts()
                             ->Mutable(0)
                             ->mutable_typed_per_filter_config();

          (*config)["envoy.filters.http.basic_auth"].PackFrom(per_route_config);
        });
    config_helper_.prependFilter(BasicAuthFilterConfig);
    initialize();
  }
};

// BasicAuth integration tests that should run with all protocols
class BasicAuthIntegrationTestAllProtocols : public BasicAuthIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, BasicAuthIntegrationTestAllProtocols,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Request with valid credential
TEST_P(BasicAuthIntegrationTestAllProtocols, ValidCredential) {
  initializeFilter();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Basic dXNlcjE6dGVzdDE="}, // user1, test1
  });

  waitForNextUpstreamRequest();

  const auto username_entry = upstream_request_->headers().get(Http::LowerCaseString("x-username"));
  EXPECT_FALSE(username_entry.empty());
  EXPECT_EQ(username_entry[0]->value().getStringView(), "user1");

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Request without credential
TEST_P(BasicAuthIntegrationTestAllProtocols, NoCredential) {
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
  EXPECT_EQ("User authentication failed. Missing username and password.", response->body());
  EXPECT_EQ(
      "Basic realm=\"http://host/\"",
      response->headers().get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());
}

// Request without wrong password
TEST_P(BasicAuthIntegrationTestAllProtocols, WrongPasswrod) {
  initializeFilter();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Basic dXNlcjE6dGVzdDI="}, // user1, test2
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ("User authentication failed. Invalid username/password combination.", response->body());
  EXPECT_EQ(
      "Basic realm=\"http://host/\"",
      response->headers().get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());
}

// Request with none-existed user
TEST_P(BasicAuthIntegrationTestAllProtocols, NoneExistedUser) {
  initializeFilter();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Basic dXNlcjM6dGVzdDI="}, // user3, test2
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ("User authentication failed. Invalid username/password combination.", response->body());
  EXPECT_EQ(
      "Basic realm=\"http://host/\"",
      response->headers().get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());
}

// Request with existing username header
TEST_P(BasicAuthIntegrationTestAllProtocols, ExistingUsernameHeader) {
  initializeFilter();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Basic dXNlcjE6dGVzdDE="}, // user1, test1
      {"x-username", "existingUsername"},
  });

  waitForNextUpstreamRequest();

  const auto username_entry = upstream_request_->headers().get(Http::LowerCaseString("x-username"));
  EXPECT_FALSE(username_entry.empty());
  EXPECT_EQ(username_entry[0]->value().getStringView(), "user1");

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(BasicAuthIntegrationTestAllProtocols, BasicAuthPerRouteDisabled) {
  disablePerRouteFilter();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
  });

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(BasicAuthIntegrationTestAllProtocols, BasicAuthPerRouteEnabled) {
  initializePerRouteFilter(AdminUsers);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Basic YWRtaW46YWRtaW4="}, // admin, admin
  });

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(BasicAuthIntegrationTestAllProtocols, BasicAuthPerRouteEnabledInvalidCredentials) {
  initializePerRouteFilter(AdminUsers);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"Authorization", "Basic dXNlcjE6dGVzdDE="}, // user1, test1
  });

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ("User authentication failed. Invalid username/password combination.", response->body());
  EXPECT_EQ(
      "Basic realm=\"http://host/\"",
      response->headers().get(Http::Headers::get().WWWAuthenticate)[0]->value().getStringView());
}

} // namespace
} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
