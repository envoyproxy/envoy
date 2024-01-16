#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {
namespace {

class BasicAuthIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initializeFilter() {
    // user1, test1
    // user2, test2
    const std::string filter_config =
        R"EOF(
name: envoy.filters.http.basic_auth
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.basic_auth.v3.BasicAuth
  users:
    inline_string: |-
      user1:{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
      user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
)EOF";
    config_helper_.prependFilter(filter_config);
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
}
} // namespace
} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
