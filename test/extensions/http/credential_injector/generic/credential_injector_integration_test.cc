#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {
namespace {

class CredentialInjectorIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initializeFilter(const std::string& filter_config) {
    TestEnvironment::writeStringToFileForTest("credential.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: credential
    generic_secret:
      secret:
        inline_string: "Basic base64EncodedUsernamePassword")EOF",
                                              false);
    config_helper_.prependFilter(TestEnvironment::substitute(filter_config));
    initialize();
  }
};

// CredentialInjector integration tests that should run with all protocols

class CredentialInjectorIntegrationTestAllProtocols : public CredentialInjectorIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, CredentialInjectorIntegrationTestAllProtocols,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Inject credential to a request without credential
TEST_P(CredentialInjectorIntegrationTestAllProtocols, InjectCredential) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* secret = bootstrap.mutable_static_resources()->add_secrets();
    secret->set_name("credential");
    auto* generic = secret->mutable_generic_secret();
    generic->mutable_secret()->set_inline_string("Basic base64EncodedUsernamePassword");
  });

  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: basic_auth
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.generic.v3.Generic
      credential:
        name: credential
)EOF";
  initializeFilter(filter_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Basic base64EncodedUsernamePassword",
            upstream_request_->headers()
                .get(Http::LowerCaseString("Authorization"))[0]
                ->value()
                .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.credential_injector.injected")->value());
}

// Inject credential to a request without credential
TEST_P(CredentialInjectorIntegrationTestAllProtocols, InjectCredentialStaticSecret) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: true
  credential:
    name: basic_auth
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.generic.v3.Generic
      credential:
        name: credential
        sds_config:
          path_config_source:
            path: "{{ test_tmpdir }}/credential.yaml"
)EOF";
  initializeFilter(filter_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Basic base64EncodedUsernamePassword",
            upstream_request_->headers()
                .get(Http::LowerCaseString("Authorization"))[0]
                ->value()
                .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.credential_injector.injected")->value());
}

// Inject credential to a request with credential, overwrite is false
TEST_P(CredentialInjectorIntegrationTestAllProtocols, CredentialExistsOverwriteFalse) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: false
  credential:
    name: basic_auth
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.generic.v3.Generic
      credential:
        name: credential
        sds_config:
          path_config_source:
            path: "{{ test_tmpdir }}/credential.yaml"
)EOF";
  initializeFilter(filter_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("Authorization"),
                                   "Basic existingBase64EncodedUsernamePassword");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Basic existingBase64EncodedUsernamePassword",
            upstream_request_->headers()
                .get(Http::LowerCaseString("Authorization"))[0]
                ->value()
                .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ(1UL,
            test_server_->counter("http.config_test.credential_injector.already_exists")->value());
}

// Inject credential to a request with credential, overwrite is true
TEST_P(CredentialInjectorIntegrationTestAllProtocols, CredentialExistsOverwriteTrue) {
  const std::string filter_config =
      R"EOF(
name: envoy.filters.http.credential_injector
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector
  overwrite: true
  credential:
    name: basic_auth
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.generic.v3.Generic
      credential:
        name: credential
        sds_config:
          path_config_source:
            path: "{{ test_tmpdir }}/credential.yaml"
)EOF";
  initializeFilter(filter_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("Authorization"),
                                   "Basic existingBase64EncodedUsernamePassword");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  EXPECT_EQ("Basic base64EncodedUsernamePassword",
            upstream_request_->headers()
                .get(Http::LowerCaseString("Authorization"))[0]
                ->value()
                .getStringView());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.credential_injector.injected")->value());
}

} // namespace
} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
