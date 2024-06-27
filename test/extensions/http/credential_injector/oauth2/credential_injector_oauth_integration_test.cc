#include "source/extensions/http/injected_credentials/oauth2/oauth_response.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {
namespace {

class CredentialInjectorIntegrationTest : public HttpIntegrationTest,
                                          public Grpc::GrpcClientIntegrationParamTest {
public:
  CredentialInjectorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, Network::Address::IpVersion::v4) {
    enableHalfClose(true);
  }
  void initializeFilter(const std::string& filter_config) {
    TestEnvironment::writeStringToFileForTest("initial_secret.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: client-secret
    generic_secret:
      secret:
        inline_string: "")EOF",
                                              false);
    TestEnvironment::writeStringToFileForTest("client_secret.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: client-secret
    generic_secret:
      secret:
        inline_string: "test_client_secret")EOF",
                                              false);
    config_helper_.prependFilter(TestEnvironment::substitute(filter_config));
    // Add the OAuth cluster.
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto oauth_cluster = config_helper_.buildStaticCluster("oauth", 0, "127.0.0.1");
      *bootstrap.mutable_static_resources()->add_clusters() = oauth_cluster;
    });

    initialize();
  }

  void createUpstreams() override {
    // backend server
    addFakeUpstream(Http::CodecType::HTTP1);

    // oauth2 server
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void TearDown() override { test_server_.reset(); }

  virtual void checkClientSecretInRequest(absl::string_view client_secret) {
    std::string request_body = oauth2_request_->body().toString();
    const auto query_parameters =
        Http::Utility::QueryParamsMulti::parseParameters(request_body, 0, true);
    auto secret = query_parameters.getFirstValue("client_secret");

    ASSERT_TRUE(secret.has_value());
    EXPECT_EQ(secret.value(), client_secret);
  }

  void getFakeOuth2Connection() {
    AssertionResult result =
        fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, fake_oauth2_connection_);
    RELEASE_ASSERT(result, result.message());
  }

  void acceptNewStream() {
    AssertionResult result =
        fake_oauth2_connection_->waitForNewStream(*dispatcher_, oauth2_request_);
    RELEASE_ASSERT(result, result.message());
    result = oauth2_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
    ASSERT_TRUE(oauth2_request_->waitForHeadersComplete());
  }

  void waitForTokenRequestAndDontRespondWithToken() {
    getFakeOuth2Connection();
    acceptNewStream();
    checkClientSecretInRequest("test_client_secret");
    oauth2_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);
  }

  void handleOauth2TokenRequest(absl::string_view client_secret, bool success = true,
                                bool good_token = true, bool good_json = true,
                                int token_expiry = 20) {
    acceptNewStream();
    checkClientSecretInRequest(client_secret);
    if (success) {
      oauth2_request_->encodeHeaders(
          Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
          false);
    } else {
      oauth2_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, false);
    }

    envoy::extensions::http::injected_credentials::oauth2::OAuthResponse oauth_response;
    if (!good_json) {
      Buffer::OwnedImpl buffer("bad json");
      oauth2_request_->encodeData(buffer, true);
      return;
    }
    oauth_response.mutable_access_token()->set_value("test-access-token");
    if (good_token) {
      oauth_response.mutable_expires_in()->set_value(token_expiry);
    }
    Buffer::OwnedImpl buffer(MessageUtil::getJsonStringFromMessageOrError(oauth_response));

    oauth2_request_->encodeData(buffer, true);
  }

  void waitForOAuth2Response(absl::string_view client_secret) {
    getFakeOuth2Connection();
    handleOauth2TokenRequest(client_secret);
  }

  void waitForBadOAuth2Response(absl::string_view client_secret) {
    getFakeOuth2Connection();
    handleOauth2TokenRequest(client_secret, false);
  }

  FakeHttpConnectionPtr fake_oauth2_connection_{};
  FakeStreamPtr oauth2_request_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsAndGrpcTypes, CredentialInjectorIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// Inject credential to a request without credential
TEST_P(CredentialInjectorIntegrationTest, InjectCredentialStaticSecret) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_endpoint:
        cluster: oauth
        timeout: 3s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: test-client-secret
)EOF";
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* secret = bootstrap.mutable_static_resources()->add_secrets();
    secret->set_name("test-client-secret");
    auto* generic = secret->mutable_generic_secret();
    generic->mutable_secret()->set_inline_string("test_client_secret");
  });
  initializeFilter(filter_config);
  waitForOAuth2Response("test_client_secret");
  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_fetched", 1,
                                 std::chrono::milliseconds(2500));
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Bearer test-access-token", upstream_request_->headers()
                                            .get(Http::LowerCaseString("Authorization"))[0]
                                            ->value()
                                            .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Inject credential to a request without credential
TEST_P(CredentialInjectorIntegrationTest, InjectCredential) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_endpoint:
        cluster: oauth
        timeout: 3s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/client_secret.yaml"
)EOF";
  initializeFilter(filter_config);
  waitForOAuth2Response("test_client_secret");
  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_fetched", 1,
                                 std::chrono::milliseconds(2500));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Bearer test-access-token", upstream_request_->headers()
                                            .get(Http::LowerCaseString("Authorization"))[0]
                                            ->value()
                                            .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Inject credential to a request with credential, overwrite is false
TEST_P(CredentialInjectorIntegrationTest, CredentialExistsOverwriteFalse) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_endpoint:
        cluster: oauth
        timeout: 3s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/client_secret.yaml"
)EOF";
  initializeFilter(filter_config);
  waitForOAuth2Response("test_client_secret");
  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_fetched", 1,
                                 std::chrono::milliseconds(2500));
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("Authorization"),
                                   "Bearer existingAccessToken");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Bearer existingAccessToken", upstream_request_->headers()
                                              .get(Http::LowerCaseString("Authorization"))[0]
                                              ->value()
                                              .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ(1UL,
            test_server_->counter("http.config_test.credential_injector.already_exists")->value());
  EXPECT_EQ(1UL,
            test_server_->counter("http.config_test.credential_injector.oauth2.token_requested")
                ->value());
  EXPECT_EQ(
      1UL,
      test_server_->counter("http.config_test.credential_injector.oauth2.token_fetched")->value());
}

// Inject credential to a request with credential, overwrite is true
TEST_P(CredentialInjectorIntegrationTest, CredentialExistsOverwriteTrue) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: true
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_endpoint:
        cluster: oauth
        timeout: 3s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/client_secret.yaml"
)EOF";
  initializeFilter(filter_config);
  waitForOAuth2Response("test_client_secret");
  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_fetched", 1,
                                 std::chrono::milliseconds(2500));
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("Authorization"),
                                   "Bearer existingAccessToken");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Bearer test-access-token", upstream_request_->headers()
                                            .get(Http::LowerCaseString("Authorization"))[0]
                                            ->value()
                                            .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.credential_injector.injected")->value());
  EXPECT_EQ(1UL,
            test_server_->counter("http.config_test.credential_injector.oauth2.token_requested")
                ->value());
  EXPECT_EQ(
      1UL,
      test_server_->counter("http.config_test.credential_injector.oauth2.token_fetched")->value());
}

// Retry token request on client secret update
TEST_P(CredentialInjectorIntegrationTest, RetryTokenRequestOnSecretUpdate) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_endpoint:
        cluster: oauth
        timeout: 0.5s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/initial_secret.yaml"
)EOF";
  initializeFilter(filter_config);
  test_server_->waitForCounterEq(
      "http.config_test.credential_injector.oauth2.token_fetch_failed_on_client_secret", 2,
      std::chrono::milliseconds(5000));
  EXPECT_EQ(0UL,
            test_server_->counter("http.config_test.credential_injector.oauth2.token_requested")
                ->value());
  EXPECT_EQ(
      0UL,
      test_server_->counter("http.config_test.credential_injector.oauth2.token_fetched")->value());

  // Update the client secret and now token request should succeed after retry
  TestEnvironment::renameFile(TestEnvironment::temporaryPath("client_secret.yaml"),
                              TestEnvironment::temporaryPath("initial_secret.yaml"));
  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_requested", 1,
                                 std::chrono::milliseconds(2500));

  waitForOAuth2Response("test_client_secret");

  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_fetched", 1,
                                 std::chrono::milliseconds(2500));

  EXPECT_EQ(1UL,
            test_server_->counter("http.config_test.credential_injector.oauth2.token_requested")
                ->value());

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Bearer test-access-token", upstream_request_->headers()
                                            .get(Http::LowerCaseString("Authorization"))[0]
                                            ->value()
                                            .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Retry token request on oauth2 server bad response
TEST_P(CredentialInjectorIntegrationTest, RetryTokenRequestOnBadResponseFromIDPServer) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_fetch_retry_interval: 2s
      scopes:
        - "scope1"
      token_endpoint:
        cluster: oauth
        timeout: 0.5s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/client_secret.yaml"
)EOF";
  initializeFilter(filter_config);

  // wait for first token request and respond with bad response
  waitForBadOAuth2Response("test_client_secret");

  // wait for retried token request and respond with good response
  handleOauth2TokenRequest("test_client_secret");

  EXPECT_EQ(
      1UL,
      test_server_
          ->counter(
              "http.config_test.credential_injector.oauth2.token_fetch_failed_on_bad_response_code")
          ->value());
  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_fetched", 1,
                                 std::chrono::milliseconds(2500));

  EXPECT_EQ(2UL,
            test_server_->counter("http.config_test.credential_injector.oauth2.token_requested")
                ->value());

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Bearer test-access-token", upstream_request_->headers()
                                            .get(Http::LowerCaseString("Authorization"))[0]
                                            ->value()
                                            .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(CredentialInjectorIntegrationTest, RefreshTokenOnHalfLife) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_fetch_retry_interval: 4s
      token_endpoint:
        cluster: oauth
        timeout: 0.5s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/client_secret.yaml"
)EOF";
  initializeFilter(filter_config);

  getFakeOuth2Connection();
  handleOauth2TokenRequest("test_client_secret", true, true, true, 2);

  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_fetched", 1,
                                 std::chrono::milliseconds(500));

  EXPECT_EQ(1UL,
            test_server_->counter("http.config_test.credential_injector.oauth2.token_requested")
                ->value());

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Bearer test-access-token", upstream_request_->headers()
                                            .get(Http::LowerCaseString("Authorization"))[0]
                                            ->value()
                                            .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // wait for second token refresh request and respond with new token
  handleOauth2TokenRequest("test_client_secret");

  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_fetched", 2,
                                 std::chrono::milliseconds(1200));
}

TEST_P(CredentialInjectorIntegrationTest, BadTokenNoExpiry) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_fetch_retry_interval: 1s
      token_endpoint:
        cluster: oauth
        timeout: 4s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/client_secret.yaml"
)EOF";
  initializeFilter(filter_config);
  getFakeOuth2Connection();
  handleOauth2TokenRequest("test_client_secret", true, false);
  test_server_->waitForCounterEq(
      "http.config_test.credential_injector.oauth2.token_fetch_failed_on_bad_token", 1,
      std::chrono::milliseconds(1000));
}

TEST_P(CredentialInjectorIntegrationTest, BadTokenMalformedJson) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_fetch_retry_interval: 1s
      token_endpoint:
        cluster: oauth
        timeout: 4s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/client_secret.yaml"
)EOF";
  initializeFilter(filter_config);
  getFakeOuth2Connection();
  handleOauth2TokenRequest("test_client_secret", true, true, false);
  test_server_->waitForCounterEq(
      "http.config_test.credential_injector.oauth2.token_fetch_failed_on_bad_token", 1,
      std::chrono::milliseconds(1000));
}

TEST_P(CredentialInjectorIntegrationTest, RetryOnClusterNotFound) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_fetch_retry_interval: 1s
      token_endpoint:
        cluster: non-existing-cluster
        timeout: 0.5s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/client_secret.yaml"
)EOF";
  initializeFilter(filter_config);
  test_server_->waitForCounterEq(
      "http.config_test.credential_injector.oauth2.token_fetch_failed_on_cluster_not_found", 1,
      std::chrono::milliseconds(1300));
  test_server_->waitForCounterEq(
      "http.config_test.credential_injector.oauth2.token_fetch_failed_on_cluster_not_found", 2,
      std::chrono::milliseconds(1300));
}

TEST_P(CredentialInjectorIntegrationTest, RetryOnStreamReset) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: envoy.http.injected_credentials.oauth2
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2
      token_fetch_retry_interval: 1s
      token_endpoint:
        cluster: oauth
        timeout: 0.5s
        uri: "oauth.com/token"
      client_credentials:
        client_id: test_client_id
        client_secret:
          name: client-secret
          sds_config:
            path_config_source:
              path: "{{ test_tmpdir }}/client_secret.yaml"
)EOF";
  initializeFilter(filter_config);
  waitForTokenRequestAndDontRespondWithToken();
  test_server_->waitForCounterEq(
      "http.config_test.credential_injector.oauth2.token_fetch_failed_on_stream_reset", 1,
      std::chrono::milliseconds(1000));
  EXPECT_EQ(1UL,
            test_server_->counter("http.config_test.credential_injector.oauth2.token_requested")
                ->value());
  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_requested", 2,
                                 std::chrono::milliseconds(1200));
  // wait for retried token request and respond with good response
  handleOauth2TokenRequest("test_client_secret");
  test_server_->waitForCounterEq("http.config_test.credential_injector.oauth2.token_fetched", 1,
                                 std::chrono::milliseconds(1200));

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Bearer test-access-token", upstream_request_->headers()
                                            .get(Http::LowerCaseString("Authorization"))[0]
                                            ->value()
                                            .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
