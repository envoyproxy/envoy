#include <chrono>
#include <thread>

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "test/config/integration/certs/client_ecdsacert_hash.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/integration/quic_http_integration_test.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {

class QuicClientCertIntegrationTest : public Quic::QuicHttpIntegrationTestBase,
                                      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  QuicClientCertIntegrationTest()
      : Quic::QuicHttpIntegrationTestBase(GetParam(), ConfigHelper::quicHttpProxyConfig()) {
    // Set the standard HTTP/3 ALPN protocol.
    client_alpn_ = "h3";
  }

  void initialize() override {
    // Configure QUIC downstream transport with client certificate support.
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* transport_socket = filter_chain->mutable_transport_socket();

      // Configure QUIC downstream transport socket.
      transport_socket->set_name("envoy.transport_sockets.quic");
      auto* typed_config = transport_socket->mutable_typed_config();
      typed_config->set_type_url(
          "type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport");

      envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport quic_config;
      auto* tls_context = quic_config.mutable_downstream_tls_context();

      // Set require_client_certificate if needed for this test.
      if (require_client_certificate_) {
        tls_context->mutable_require_client_certificate()->set_value(true);
      }

      auto* common_tls = tls_context->mutable_common_tls_context();

      // Add server certificate using standard test certificates.
      auto* tls_cert = common_tls->add_tls_certificates();
      tls_cert->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
      tls_cert->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));

      // Always configure validation context since we want to validate client certs when provided.
      auto* validation_context = common_tls->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));

      // Configure trust chain verification behavior for client certificate enforcement
      if (require_client_certificate_) {
        validation_context->set_trust_chain_verification(
            envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
                VERIFY_TRUST_CHAIN);
      } else {
        validation_context->set_trust_chain_verification(
            envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
                ACCEPT_UNTRUSTED);
      }

      // Add certificate hash validation for specific tests.
      if (use_client_certificate_) {
        if (use_ecdsa_client_cert_) {
          validation_context->add_verify_certificate_hash(TEST_CLIENT_ECDSA_CERT_HASH);
        } else {
          validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);
        }
      }

      // Configure SAN matching if specified.
      if (!expected_client_san_.empty()) {
        auto* san_matcher = validation_context->add_match_typed_subject_alt_names();
        san_matcher->mutable_matcher()->set_exact(expected_client_san_);
        san_matcher->set_san_type(
            envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
      }

      typed_config->PackFrom(quic_config);
    });

    // Use the base class initialization.
    Quic::QuicHttpIntegrationTestBase::initialize();
  }

protected:
  // Test configuration flags.
  bool require_client_certificate_{false};
  bool use_client_certificate_{false};
  bool use_ecdsa_client_cert_{false};
  std::string expected_client_san_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicClientCertIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Server requires client certificate, client provides valid RSA certificate.
TEST_P(QuicClientCertIntegrationTest, ValidClientCertificateRsa) {
  require_client_certificate_ = true;
  use_client_certificate_ = true;
  use_ecdsa_client_cert_ = false;
  expected_client_san_ = "spiffe://lyft.com/test-team";

  // Configure SSL options for RSA client certificate.
  ssl_client_option_.setClientEcdsaCert(false);

  initialize();

  // Create HTTP client connection that will use QUIC transport.
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Test that the connection is established successfully.
  EXPECT_TRUE(codec_client_->connected())
      << "Connection with valid RSA client certificate should succeed";

  // Send a simple request to verify the connection works.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Clean up.
  codec_client_->close();
}

// Server requires client certificate, client provides valid ECDSA certificate.
TEST_P(QuicClientCertIntegrationTest, ValidClientCertificateEcdsa) {
  require_client_certificate_ = true;
  use_client_certificate_ = true;
  use_ecdsa_client_cert_ = true;
  expected_client_san_ = "spiffe://lyft.com/test-team";

  // Configure SSL options for ECDSA client certificate.
  ssl_client_option_.setClientEcdsaCert(true);

  initialize();

  // Create HTTP client connection that will use QUIC transport with ECDSA cert.
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Test that the connection is established successfully.
  EXPECT_TRUE(codec_client_->connected())
      << "Connection with valid ECDSA client certificate should succeed";

  // Send a simple request to verify the connection works.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Clean up.
  codec_client_->close();
}

// Server requires client certificate, client provides no certificate.
// In this case, connection establishes but HTTP requests timeout/fail.
TEST_P(QuicClientCertIntegrationTest, MissingRequiredClientCertificate) {
  require_client_certificate_ = true;
  use_client_certificate_ = false;

  // Configure SSL options without client certificate.
  ssl_client_option_.setProvideClientCertificate(false);

  initialize();

  // QUIC allows connection establishment but prevents HTTP request completion
  // when client certificates are missing/invalid.
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);

  // QUIC should allow connection establishment.
  EXPECT_TRUE(codec_client_->connected())
      << "QUIC should allow initial connection establishment even without client certificate";

  // But HTTP requests should fail/timeout due to client certificate validation.
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "https"}, {":authority", "host"}});

  // Request should NOT complete within reasonable time.
  bool request_completed = response->waitForEndStream(std::chrono::milliseconds(2000));

  EXPECT_FALSE(request_completed)
      << "HTTP request should timeout/fail when client certificate is missing.\n"
      << "This timeout indicates QUIC client certificate validation is working correctly.\n"
      << "Connection state: connected=" << codec_client_->connected()
      << ", disconnected=" << codec_client_->disconnected();

  // Cleanup.
  cleanupUpstreamAndDownstream();
  codec_client_.reset();
}

// Server doesn't require client certificate, client provides no certificate.
TEST_P(QuicClientCertIntegrationTest, OptionalClientCertificateNotProvided) {
  require_client_certificate_ = false;
  use_client_certificate_ = false;

  // Configure SSL options without client certificate.
  ssl_client_option_.setProvideClientCertificate(false);

  initialize();

  // Create HTTP client connection. This should succeed without client cert when optional.
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Connection should succeed even without client certificate.
  EXPECT_TRUE(codec_client_->connected())
      << "Connection should succeed when client certificate is optional";

  // Send a simple request to verify the connection works.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Clean up.
  codec_client_->close();
}

// Server requires client certificate, client provides certificate with wrong SAN.
// In this case, connection establishes but HTTP requests timeout/fail.
TEST_P(QuicClientCertIntegrationTest, ClientCertificateWithWrongSan) {
  require_client_certificate_ = true;
  use_client_certificate_ = false; // Don't validate certificate hash, only SAN
  use_ecdsa_client_cert_ = false;
  expected_client_san_ = "spiffe://lyft.com/test-team"; // Server expects this SAN

  // Configure SSL options to provide RSA client certificate but with different SAN than expected.
  ssl_client_option_.setClientEcdsaCert(false).setSan("spiffe://lyft.com/wrong-team");

  initialize();

  // Test SAN validation behavior.
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);

  // QUIC should allow connection establishment even with wrong SAN.
  EXPECT_TRUE(codec_client_->connected())
      << "QUIC should allow initial connection establishment even with wrong SAN certificate";

  // HTTP requests should fail/timeout due to SAN validation.
  auto response = codec_client_->makeHeaderOnlyRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "https"}, {":authority", "host"}});

  // Request should NOT complete within reasonable time.
  bool request_completed = response->waitForEndStream(std::chrono::milliseconds(2500));

  EXPECT_FALSE(request_completed)
      << "HTTP request should timeout/fail when client certificate SAN is wrong.\n"
      << "Expected SAN: '" << expected_client_san_
      << "', Provided SAN: 'spiffe://lyft.com/wrong-team'\n"
      << "This timeout indicates QUIC SAN validation is working correctly.\n"
      << "Connection state: connected=" << codec_client_->connected()
      << ", disconnected=" << codec_client_->disconnected();

  // Cleanup.
  cleanupUpstreamAndDownstream();
  codec_client_.reset();
}

// Server allows optional client certificate, client provides no certificate.
// Verify this works for HTTP processing like it does for H2.
TEST_P(QuicClientCertIntegrationTest, OptionalClientCertificateWithHttpProcessing) {
  require_client_certificate_ = false;
  use_client_certificate_ = false;

  // Configure SSL options without client certificate.
  ssl_client_option_.setProvideClientCertificate(false);

  initialize();

  // Create HTTP client connection. This should succeed without client cert.
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Connection should succeed without client certificate
  EXPECT_TRUE(codec_client_->connected())
      << "Connection should succeed when client certificate is optional";

  // Send a request to verify normal HTTP processing works.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);

  // Verify that the connection info reflects no client certificate in headers.
  // When no client cert is provided, the x-forwarded-client-cert header should not be present.
  auto client_cert_header =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  EXPECT_TRUE(client_cert_header.empty())
      << "No client certificate header should be present when no cert is provided";

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Clean up.
  codec_client_->close();
}

// Basic server configuration validation. We verify that server accepts client cert config.
TEST_P(QuicClientCertIntegrationTest, ServerConfigurationAcceptsClientCertRequirement) {
  require_client_certificate_ = true;
  use_client_certificate_ = true;

  // This test should pass initialize() without throwing.
  EXPECT_NO_THROW(initialize());

  // Verify the server was created successfully and the configuration was accepted.
  EXPECT_NE(test_server_, nullptr);
}

} // namespace Envoy
