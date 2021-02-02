#include "common/protobuf/utility.h"

#include "source/extensions/filters/http/oauth2/oauth_response.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth {
namespace {

class OauthIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  OauthIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, Network::Address::IpVersion::v4) {
    enableHalfClose(true);
  }

  envoy::service::discovery::v3::DiscoveryResponse genericSecretResponse(absl::string_view name,
                                                                         absl::string_view value) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(std::string(name));
    secret.mutable_generic_secret()->mutable_secret()->set_inline_string(std::string(value));

    envoy::service::discovery::v3::DiscoveryResponse response_pb;
    response_pb.add_resources()->PackFrom(secret);
    response_pb.set_type_url(
        envoy::extensions::transport_sockets::tls::v3::Secret::descriptor()->name());
    return response_pb;
  }

  void initialize() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

    config_helper_.addFilter(R"EOF(
name: oauth
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3alpha.OAuth2
  config:
    token_endpoint:
      cluster: oauth
      uri: oauth.com/token
      timeout: 3s
    authorization_endpoint: https://oauth.com/oauth/authorize/
    redirect_uri: "%REQ(:x-forwarded-proto)%://%REQ(:authority)%/callback"
    redirect_path_matcher: 
      path:
        exact: /callback
    signout_path: 
      path:
        exact: /signout
    credentials:
      client_id: foo
      token_secret:
        name: token
      hmac_secret:
        name: hmac
    auth_scopes:
    - user
    - openid
    - email
)EOF");

    // Add the OAuth cluster.
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_static_resources()->add_clusters() =
          config_helper_.buildStaticCluster("oauth", 0, "127.0.0.1");

      auto* token_secret = bootstrap.mutable_static_resources()->add_secrets();
      token_secret->set_name("token");
      token_secret->mutable_generic_secret()->mutable_secret()->set_inline_bytes("token_secret");

      auto* hmac_secret = bootstrap.mutable_static_resources()->add_secrets();
      hmac_secret->set_name("hmac");
      hmac_secret->mutable_generic_secret()->mutable_secret()->set_inline_bytes("hmac_secret");
    });

    setUpstreamCount(2);

    HttpIntegrationTest::initialize();
  }
};

// Regular request gets redirected to the login page.
TEST_F(OauthIntegrationTest, UnauthenticatedFlow) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":path", "/lua/per/route/default"},
                                         {":scheme", "http"},
                                         {":authority", "authority"}};
  auto encoder_decoder = codec_client_->startRequest(headers);

  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // We should get an immediate redirect back.
  response->waitForHeaders();
  EXPECT_EQ("302", response->headers().getStatusValue());
}

TEST_F(OauthIntegrationTest, AuthenticationFlow) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"},
      {":path", "/callback?code=foo&state=http%3A%2F%2Ftraffic.example.com%2Fnot%2F_oauth"},
      {":scheme", "http"},
      {"x-forwarded-proto", "http"},
      {":authority", "authority"},
      {"authority", "Bearer token"}};
  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  waitForNextUpstreamRequest(1);

  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  upstream_request_->encodeHeaders(
      Http::TestRequestHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);

  envoy::extensions::http_filters::oauth2::OAuthResponse oauth_response;
  oauth_response.mutable_access_token()->set_value("bar");
  oauth_response.mutable_expires_in()->set_value(
      std::chrono::duration_cast<std::chrono::seconds>(
          api_->timeSource().systemTime().time_since_epoch() + std::chrono::seconds(10))
          .count());

  Buffer::OwnedImpl buffer(MessageUtil::getJsonStringFromMessageOrDie(oauth_response));
  upstream_request_->encodeData(buffer, true);

  // We should get an immediate redirect back.
  response->waitForHeaders();
  EXPECT_EQ("302", response->headers().getStatusValue());
}

} // namespace
} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
