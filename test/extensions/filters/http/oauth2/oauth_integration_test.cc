#include "common/crypto/utility.h"
#include "common/protobuf/utility.h"

#include "source/extensions/filters/http/oauth2/oauth_response.pb.h"

#include "test/integration/http_integration.h"

#include "absl/strings/escaping.h"
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

    config_helper_.addFilter(TestEnvironment::substitute(R"EOF(
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
        sds_config:
          path: "{{ test_tmpdir }}/token_secret.yaml"
      hmac_secret:
        name: hmac
        sds_config:
          path: "{{ test_tmpdir }}/hmac_secret.yaml"
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

std::string parseSetCookieValue(const Http::HeaderMap& headers, const std::string& key) {

  std::string ret;

  headers.iterateReverse([&key, &ret](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    // Find the cookie headers in the request (typically, there's only one).
    if (header.key() == Http::Headers::get().SetCookie.get()) {

      // Split the cookie header into individual cookies.
      for (const auto& s : StringUtil::splitToken(header.value().getStringView(), ";")) {
        // Find the key part of the cookie (i.e. the name of the cookie).
        size_t first_non_space = s.find_first_not_of(" ");
        size_t equals_index = s.find('=');
        if (equals_index == absl::string_view::npos) {
          // The cookie is malformed if it does not have an `=`. Continue
          // checking other cookies in this header.
          continue;
        }
        const absl::string_view k = s.substr(first_non_space, equals_index - first_non_space);
        // If the key matches, parse the value from the rest of the cookie string.
        if (k == key) {
          absl::string_view v = s.substr(equals_index + 1, s.size() - 1);

          // Cookie values may be wrapped in double quotes.
          // https://tools.ietf.org/html/rfc6265#section-4.1.1
          if (v.size() >= 2 && v.back() == '"' && v[0] == '"') {
            v = v.substr(1, v.size() - 2);
          }
          ret = std::string{v};
          return Http::HeaderMap::Iterate::Break;
        }
      }
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  return ret;
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
  absl::string_view host_ = headers.Host()->value().getStringView();

  TestEnvironment::renameFile(TestEnvironment::temporaryPath("token_secret_1.yaml"),
                              TestEnvironment::temporaryPath("token_secret.yaml"));

  TestEnvironment::renameFile(TestEnvironment::temporaryPath("hmac_secret_1.yaml"),
                              TestEnvironment::temporaryPath("hmac_secret.yaml"));

  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  waitForNextUpstreamRequest(1);

  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  std::string request_body = upstream_request_->body().toString();
  const auto query_parameters = Http::Utility::parseFromBody(request_body);
  auto it = query_parameters.find("client_secret");

  ASSERT_TRUE(it != query_parameters.end());
  EXPECT_EQ(it->second, "token_secret_1");

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

  const Http::ResponseHeaderMap& headers_ = response->headers();
  std::string expires_ = parseSetCookieValue(headers_, "OauthExpires");
  std::string token_ = parseSetCookieValue(headers_, "BearerToken");
  std::string hmac_ = parseSetCookieValue(headers_, "OauthHMAC");
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto hmac_payload = absl::StrCat(host_, expires_, token_);
  std::string hmac_secret = "hmac_secret_1";
  std::vector<uint8_t> hmac_secret_vec(hmac_secret.begin(), hmac_secret.end());
  const auto pre_encoded_hmac =
      Hex::encode(crypto_util.getSha256Hmac(hmac_secret_vec, hmac_payload));
  std::string encoded_hmac;
  absl::Base64Escape(pre_encoded_hmac, &encoded_hmac);

  EXPECT_EQ(encoded_hmac, hmac_) << response->headers();

  EXPECT_EQ("302", response->headers().getStatusValue());
}

} // namespace
} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
