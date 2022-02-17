#include "source/common/crypto/utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/oauth2/filter.h"
#include "source/extensions/filters/http/oauth2/oauth_response.pb.h"

#include "test/integration/http_integration.h"

#include "absl/strings/escaping.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {
namespace {

class OauthIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  OauthIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, Network::Address::IpVersion::v4) {
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
    setUpstreamProtocol(Http::CodecType::HTTP2);

    TestEnvironment::writeStringToFileForTest("token_secret.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: token
    generic_secret:
      secret:
        inline_string: "token_secret")EOF",
                                              false);

    TestEnvironment::writeStringToFileForTest("token_secret_1.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: token
    generic_secret:
      secret:
        inline_string: "token_secret_1")EOF",
                                              false);

    TestEnvironment::writeStringToFileForTest("hmac_secret.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: hmac
    generic_secret:
      secret:
        inline_string: "hmac_secret")EOF",
                                              false);

    TestEnvironment::writeStringToFileForTest("hmac_secret_1.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: hmac
    generic_secret:
      secret:
        inline_string: "hmac_secret_1")EOF",
                                              false);

    config_helper_.prependFilter(TestEnvironment::substitute(R"EOF(
name: oauth
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3.OAuth2
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
        sds_config:
          path_config_source:
            path: "{{ test_tmpdir }}/token_secret.yaml"
          resource_api_version: V3
      hmac_secret:
        name: hmac
        sds_config:
          path_config_source:
            path: "{{ test_tmpdir }}/hmac_secret.yaml"
          resource_api_version: V3
    auth_scopes:
    - user
    - openid
    - email
    resources:
    - oauth2-resource
    - http://example.com
    - https://example.com
)EOF"));

    // Add the OAuth cluster.
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_static_resources()->add_clusters() =
          config_helper_.buildStaticCluster("oauth", 0, "127.0.0.1");
    });

    setUpstreamCount(2);

    HttpIntegrationTest::initialize();
  }

  bool validateHmac(const Http::ResponseHeaderMap& headers, absl::string_view host,
                    absl::string_view hmac_secret) {
    std::string expires =
        Http::Utility::parseSetCookieValue(headers, default_cookie_names_.oauth_expires_);
    std::string token =
        Http::Utility::parseSetCookieValue(headers, default_cookie_names_.bearer_token_);
    std::string hmac =
        Http::Utility::parseSetCookieValue(headers, default_cookie_names_.oauth_hmac_);

    Http::TestRequestHeaderMapImpl validate_headers{{":authority", std::string(host)}};

    validate_headers.addReferenceKey(Http::Headers::get().Cookie,
                                     absl::StrCat(default_cookie_names_.oauth_hmac_, "=", hmac));
    validate_headers.addReferenceKey(
        Http::Headers::get().Cookie,
        absl::StrCat(default_cookie_names_.oauth_expires_, "=", expires));
    validate_headers.addReferenceKey(Http::Headers::get().Cookie,
                                     absl::StrCat(default_cookie_names_.bearer_token_, "=", token));

    OAuth2CookieValidator validator{api_->timeSource(), default_cookie_names_};
    validator.setParams(validate_headers, std::string(hmac_secret));
    return validator.isValid();
  }

  void doAuthenticationFlow(absl::string_view token_secret, absl::string_view hmac_secret) {
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

    std::string request_body = upstream_request_->body().toString();
    const auto query_parameters = Http::Utility::parseFromBody(request_body);
    auto it = query_parameters.find("client_secret");

    ASSERT_TRUE(it != query_parameters.end());
    EXPECT_EQ(it->second, token_secret);

    upstream_request_->encodeHeaders(
        Http::TestRequestHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
        false);

    envoy::extensions::http_filters::oauth2::OAuthResponse oauth_response;
    oauth_response.mutable_access_token()->set_value("bar");
    oauth_response.mutable_expires_in()->set_value(DateUtil::nowToSeconds(api_->timeSource()) + 10);

    Buffer::OwnedImpl buffer(MessageUtil::getJsonStringFromMessageOrDie(oauth_response));
    upstream_request_->encodeData(buffer, true);

    // We should get an immediate redirect back.
    response->waitForHeaders();

    EXPECT_TRUE(
        validateHmac(response->headers(), headers.Host()->value().getStringView(), hmac_secret));

    EXPECT_EQ("302", response->headers().getStatusValue());

    RELEASE_ASSERT(response->waitForEndStream(), "unexpected timeout");
    codec_client_->close();
  }

  const CookieNames default_cookie_names_{"BearerToken", "OauthHMAC", "OauthExpires"};
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

  // 1. Do one authentication flow.
  doAuthenticationFlow("token_secret", "hmac_secret");

  // 2. Reload secrets.
  EXPECT_EQ(test_server_->counter("sds.token.update_success")->value(), 1);
  EXPECT_EQ(test_server_->counter("sds.hmac.update_success")->value(), 1);
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("token_secret_1.yaml"),
                              TestEnvironment::temporaryPath("token_secret.yaml"));
  test_server_->waitForCounterEq("sds.token.update_success", 2, std::chrono::milliseconds(5000));
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("hmac_secret_1.yaml"),
                              TestEnvironment::temporaryPath("hmac_secret.yaml"));
  test_server_->waitForCounterEq("sds.hmac.update_success", 2, std::chrono::milliseconds(5000));
  // 3. Do another one authentication flow.
  doAuthenticationFlow("token_secret_1", "hmac_secret_1");
}

} // namespace
} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
