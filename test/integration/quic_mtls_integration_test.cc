#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/quic/quic_ssl_connection_info.h"
#include "source/common/tls/cert_validator/san_matcher.h"

#include "test/integration/quic_http_integration_test.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class QuicMtlsIntegrationTest : public QuicHttpIntegrationTestBase,
                                public testing::TestWithParam<Network::Address::IpVersion> {
public:
  QuicMtlsIntegrationTest()
      : QuicHttpIntegrationTestBase(GetParam(),
                                    ConfigHelper::httpProxyConfig(/*downstream_use_quic=*/true)) {}

  void initialize() override {
    // Configure client with certificates for mTLS testing using defaults.
    ssl_client_option_.setSan("spiffe://lyft.com/frontend-team").setSni("lyft.com");
    QuicHttpIntegrationTestBase::initialize();
  }

  void setupServerWithClientCertValidation(const std::string& client_ca_cert = "cacert.pem",
                                           const std::string& server_cert = "servercert.pem",
                                           const std::string& server_key = "serverkey.pem",
                                           bool require_client_cert = true,
                                           const std::string& trusted_ca_cert = "") {
    config_helper_.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_filter_chains(0)
                                   ->mutable_transport_socket();

      envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport quic_config;
      auto unpack_result =
          MessageUtil::unpackTo(*transport_socket->mutable_typed_config(), quic_config);
      ASSERT_TRUE(unpack_result.ok());

      auto* tls_context = quic_config.mutable_downstream_tls_context();
      tls_context->mutable_require_client_certificate()->set_value(require_client_cert);

      // Set up server certificate.
      auto* server_cert_config = tls_context->mutable_common_tls_context()->add_tls_certificates();
      server_cert_config->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/" + server_cert));
      server_cert_config->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/" + server_key));

      // Set up CA for client cert validation.
      if (require_client_cert) {
        const std::string ca_file = trusted_ca_cert.empty() ? client_ca_cert : trusted_ca_cert;
        tls_context->mutable_common_tls_context()
            ->mutable_validation_context()
            ->mutable_trusted_ca()
            ->set_filename(
                TestEnvironment::runfilesPath("test/config/integration/certs/" + ca_file));
      }

      transport_socket->mutable_typed_config()->PackFrom(quic_config);
    });

    setupXfccHeaderConfig();
  }

  void setupXfccHeaderConfig() {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.set_forward_client_cert_details(
              envoy::extensions::filters::network::http_connection_manager::v3::
                  HttpConnectionManager::SANITIZE_SET);

          auto* cert_details = hcm.mutable_set_current_client_cert_details();
          cert_details->mutable_subject()->set_value(true);
          cert_details->set_cert(true);
          cert_details->set_chain(true);
          cert_details->set_uri(true);
          cert_details->set_dns(true);
          // Note: set_hash method may not be available in all Envoy versions.
        });
  }

  void verifyServerSideClientCertInfo(const std::string& expected_subject_contains = "") {
    // Check that XFCC header is present and populated.
    auto xfcc_headers =
        upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
    ASSERT_FALSE(xfcc_headers.empty()) << "XFCC header should be present with client certificate";

    std::string xfcc_value = std::string(xfcc_headers[0]->value().getStringView());
    EXPECT_FALSE(xfcc_value.empty()) << "XFCC header should not be empty";

    if (!expected_subject_contains.empty()) {
      EXPECT_THAT(xfcc_value, testing::HasSubstr("Subject="))
          << "Should contain certificate subject";
      EXPECT_THAT(xfcc_value, testing::HasSubstr(expected_subject_contains))
          << "Should contain expected subject part";
    }

    ENVOY_LOG_MISC(info, "XFCC Header: {}", xfcc_value);
  }

  void verifyClientSideServerCertInfo() {
    auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
    ASSERT_NE(quic_session, nullptr);
    ASSERT_NE(quic_session->ssl(), nullptr);

    auto ssl_info = quic_session->ssl();

    // Test basic certificate presence and validation.
    EXPECT_TRUE(ssl_info->peerCertificatePresented()) << "Server certificate should be presented";
    EXPECT_TRUE(ssl_info->peerCertificateValidated()) << "Server certificate should be validated";

    // Test certificate information extraction.
    EXPECT_FALSE(ssl_info->sha256PeerCertificateDigest().empty());
    EXPECT_FALSE(ssl_info->subjectPeerCertificate().empty());
    EXPECT_FALSE(ssl_info->serialNumberPeerCertificate().empty());

    ENVOY_LOG_MISC(info, "Client-side server certificate validation passed");
  }

  void expectConnectionFailure(const std::string& expected_error_contains = "") {
    codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
    EXPECT_FALSE(codec_client_->connected()) << "Connection should fail";

    if (!expected_error_contains.empty() && codec_client_->connection() != nullptr) {
      std::string failure_reason =
          std::string(codec_client_->connection()->transportFailureReason());
      if (!failure_reason.empty()) {
        EXPECT_THAT(failure_reason, testing::HasSubstr(expected_error_contains))
            << "Failure reason should contain expected error";
        ENVOY_LOG_MISC(info, "Expected connection failure: {}", failure_reason);
      }
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicMtlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test successful mTLS with client certificate and XFCC header generation.
TEST_P(QuicMtlsIntegrationTest, SuccessfulMtlsWithClientCertificate) {
  setupServerWithClientCertValidation();
  initialize();

  // Make connection with client certificate.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Wait for upstream request.
  waitForNextUpstreamRequest();

  // Verify server-side client certificate validation.
  verifyServerSideClientCertInfo("Test Frontend Team");

  // Send successful response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify client-side server certificate validation.
  verifyClientSideServerCertInfo();

  codec_client_->close();
}

// Test comprehensive certificate information extraction.
TEST_P(QuicMtlsIntegrationTest, ComprehensiveCertificateInfoExtraction) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  // Get the QUIC session for detailed certificate testing.
  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  auto ssl_info = quic_session->ssl();

  // Test all certificate extraction methods.
  EXPECT_TRUE(ssl_info->peerCertificatePresented());
  EXPECT_TRUE(ssl_info->peerCertificateValidated());

  // Test digest methods with exact length validation.
  EXPECT_FALSE(ssl_info->sha256PeerCertificateDigest().empty());
  EXPECT_FALSE(ssl_info->sha1PeerCertificateDigest().empty());
  EXPECT_EQ(ssl_info->sha256PeerCertificateDigest().length(),
            64);                                                 // SHA256 = 32 bytes = 64 hex chars
  EXPECT_EQ(ssl_info->sha1PeerCertificateDigest().length(), 40); // SHA1 = 20 bytes = 40 hex chars

  // Test certificate chain digests.
  auto sha256_chain_digests = ssl_info->sha256PeerCertificateChainDigests();
  auto sha1_chain_digests = ssl_info->sha1PeerCertificateChainDigests();
  EXPECT_FALSE(sha256_chain_digests.empty());
  EXPECT_FALSE(sha1_chain_digests.empty());

  // Test SAN extraction.
  auto uri_sans = ssl_info->uriSanPeerCertificate();
  auto dns_sans = ssl_info->dnsSansPeerCertificate();

  EXPECT_FALSE(uri_sans.empty()) << "Should have URI SANs";
  EXPECT_FALSE(dns_sans.empty()) << "Should have DNS SANs";

  // Test certificate dates.
  auto valid_from = ssl_info->validFromPeerCertificate();
  auto expiration = ssl_info->expirationPeerCertificate();
  EXPECT_TRUE(valid_from.has_value()) << "Should have valid from date";
  EXPECT_TRUE(expiration.has_value()) << "Should have expiration date";

  // Test TLS connection information.
  EXPECT_FALSE(ssl_info->tlsVersion().empty());
  EXPECT_NE(ssl_info->ciphersuiteId(), 0xffff);
  EXPECT_FALSE(ssl_info->ciphersuiteString().empty());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// Test server without client certificate requirement (should succeed).
TEST_P(QuicMtlsIntegrationTest, ServerWithoutClientCertRequirement) {
  setupServerWithClientCertValidation("", "servercert.pem", "serverkey.pem", false);
  initialize();

  // Connect without client certificate when not required - should succeed.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  // No XFCC header should be present since no client cert was provided.
  auto xfcc_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  EXPECT_TRUE(xfcc_headers.empty())
      << "XFCC header should not be present without client certificate";

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify server certificate validation still works.
  verifyClientSideServerCertInfo();

  codec_client_->close();
}

// Test XFCC header comprehensive field validation.
TEST_P(QuicMtlsIntegrationTest, XfccHeaderComprehensiveValidation) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  // Validate comprehensive XFCC header content.
  auto xfcc_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  ASSERT_FALSE(xfcc_headers.empty());

  std::string xfcc_value = std::string(xfcc_headers[0]->value().getStringView());
  EXPECT_FALSE(xfcc_value.empty());

  // Validate specific XFCC fields that QUIC mTLS should provide.
  EXPECT_THAT(xfcc_value, testing::HasSubstr("Subject=")) << "Should contain certificate subject";
  EXPECT_THAT(xfcc_value, testing::HasSubstr("Hash=")) << "Should contain certificate hash";
  EXPECT_THAT(xfcc_value, testing::HasSubstr("Cert=")) << "Should contain URL-encoded certificate";

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// Test certificate chain validation with intermediate CA.
TEST_P(QuicMtlsIntegrationTest, IntermediateCaCertificateChainValidation) {
  setupServerWithClientCertValidation("intermediate_ca_cert_chain.pem");
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  // Verify certificate chain validation works.
  verifyServerSideClientCertInfo("CN=Test");

  // Verify certificate chain information in XFCC header.
  auto xfcc_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  std::string xfcc_value = std::string(xfcc_headers[0]->value().getStringView());

  // Should contain certificate chain information.
  EXPECT_THAT(xfcc_value, testing::HasSubstr("Chain=")) << "Should contain certificate chain";

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// Test multiple requests on same mTLS connection.
TEST_P(QuicMtlsIntegrationTest, MultipleRequestsOnSameMtlsConnection) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  // Send requests one by one to avoid overwhelming the test framework.
  for (int i = 0; i < 3; ++i) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

    waitForNextUpstreamRequest();

    // Each request should have XFCC header.
    verifyServerSideClientCertInfo("Test Frontend Team");

    upstream_request_->encodeHeaders(default_response_headers_, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  codec_client_->close();
}

// Test certificate validation with SAN matching.
TEST_P(QuicMtlsIntegrationTest, SubjectAlternativeNameValidation) {
  // Set up server with SAN-based validation.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* transport_socket = bootstrap.mutable_static_resources()
                                 ->mutable_listeners(0)
                                 ->mutable_filter_chains(0)
                                 ->mutable_transport_socket();

    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport quic_config;
    auto unpack_result =
        MessageUtil::unpackTo(*transport_socket->mutable_typed_config(), quic_config);
    ASSERT_TRUE(unpack_result.ok());

    auto* tls_context = quic_config.mutable_downstream_tls_context();
    tls_context->mutable_require_client_certificate()->set_value(true);

    // Set up server certificate.
    auto* server_cert = tls_context->mutable_common_tls_context()->add_tls_certificates();
    server_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    server_cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));

    // Set up CA for client cert validation with SAN matching.
    auto* validation_context =
        tls_context->mutable_common_tls_context()->mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));

    // Add SAN matcher for URI.
    auto* san_matcher = validation_context->add_match_typed_subject_alt_names();
    san_matcher->set_san_type(
        envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
    san_matcher->mutable_matcher()->set_exact("spiffe://lyft.com/frontend-team");

    transport_socket->mutable_typed_config()->PackFrom(quic_config);
  });

  setupXfccHeaderConfig();
  initialize();

  // Connect with client certificate that has matching SAN.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  // Verify SAN-based validation succeeded.
  verifyServerSideClientCertInfo("Test Frontend Team");

  // Verify specific SAN information in XFCC.
  auto xfcc_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  std::string xfcc_value = std::string(xfcc_headers[0]->value().getStringView());
  EXPECT_THAT(xfcc_value, testing::HasSubstr("URI=spiffe://lyft.com/frontend-team"))
      << "Should contain URI SAN";

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// Test TLS version and cipher suite information.
TEST_P(QuicMtlsIntegrationTest, TlsVersionAndCipherSuiteInformation) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  // Get SSL connection info for TLS details.
  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  auto ssl_info = quic_session->ssl();

  // Test TLS version information.
  EXPECT_FALSE(ssl_info->tlsVersion().empty()) << "TLS version should not be empty";

  // Test cipher suite information.
  EXPECT_NE(ssl_info->ciphersuiteId(), 0xffff) << "Cipher suite ID should be valid";
  EXPECT_FALSE(ssl_info->ciphersuiteString().empty()) << "Cipher suite string should not be empty";

  // Test ALPN negotiation.
  EXPECT_FALSE(ssl_info->alpn().empty()) << "ALPN should be negotiated";

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

TEST_P(QuicMtlsIntegrationTest, QuicSslConnectionInfoComprehensiveCoverage) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  auto ssl_info = quic_session->ssl();

  // Test issuerPeerCertificate().
  const std::string& issuer = ssl_info->issuerPeerCertificate();
  EXPECT_FALSE(issuer.empty()) << "Issuer should not be empty for valid certificate";
  ENVOY_LOG_MISC(info, "Certificate issuer: {}", issuer);

  // Test parsedSubjectPeerCertificate().
  auto parsed_subject = ssl_info->parsedSubjectPeerCertificate();
  EXPECT_TRUE(parsed_subject.has_value()) << "Parsed subject should be available";
  ENVOY_LOG_MISC(info, "Parsed subject available: {}", parsed_subject.has_value());

  // Test serialNumbersPeerCertificates().
  auto serial_numbers = ssl_info->serialNumbersPeerCertificates();
  EXPECT_FALSE(serial_numbers.empty()) << "Should have at least one serial number in chain";
  for (const auto& sn : serial_numbers) {
    EXPECT_FALSE(sn.empty()) << "Serial number should not be empty";
  }
  ENVOY_LOG_MISC(info, "Certificate chain has {} serial numbers", serial_numbers.size());

  // Test `ipSansPeerCertificate()`.
  auto ip_sans = ssl_info->ipSansPeerCertificate();
  // IP SANs may or may not be present depending on the certificate.
  ENVOY_LOG_MISC(info, "Certificate has {} IP SANs", ip_sans.size());

  // Test `emailSansPeerCertificate()`.
  auto email_sans = ssl_info->emailSansPeerCertificate();
  // Email SANs may or may not be present depending on the certificate.
  ENVOY_LOG_MISC(info, "Certificate has {} Email SANs", email_sans.size());

  // Test `othernameSansPeerCertificate()`.
  auto othername_sans = ssl_info->othernameSansPeerCertificate();
  // OtherName SANs may or may not be present depending on the certificate.
  ENVOY_LOG_MISC(info, "Certificate has {} OtherName SANs", othername_sans.size());

  // Test `oidsPeerCertificate()`.
  auto oids = ssl_info->oidsPeerCertificate();
  // OIDs should be present for most certificates.
  ENVOY_LOG_MISC(info, "Certificate has {} OIDs", oids.size());

  // Test local certificate methods.
  // These return empty for QUIC as noted in the TODO.
  auto ip_sans_local = ssl_info->ipSansLocalCertificate();
  auto email_sans_local = ssl_info->emailSansLocalCertificate();
  auto othername_sans_local = ssl_info->othernameSansLocalCertificate();
  auto oids_local = ssl_info->oidsLocalCertificate();
  EXPECT_TRUE(ip_sans_local.empty()) << "Local IP SANs should be empty for QUIC";
  EXPECT_TRUE(email_sans_local.empty()) << "Local Email SANs should be empty for QUIC";
  EXPECT_TRUE(othername_sans_local.empty()) << "Local OtherName SANs should be empty for QUIC";
  EXPECT_TRUE(oids_local.empty()) << "Local OIDs should be empty for QUIC";

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

TEST_P(QuicMtlsIntegrationTest, PeerCertificateSanMatcherCoverage) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  auto ssl_info = quic_session->ssl();

  // Use DnsExactStringSanMatcher which doesn't require a context.
  Extensions::TransportSockets::Tls::DnsExactStringSanMatcher dns_matcher("server1.lyft.com");
  bool dns_matches = ssl_info->peerCertificateSanMatches(dns_matcher);
  ENVOY_LOG_MISC(info, "DNS SAN matcher result for server1.lyft.com: {}", dns_matches);

  // Test with a non-matching domain.
  Extensions::TransportSockets::Tls::DnsExactStringSanMatcher non_matching_matcher(
      "nonexistent.example.com");
  bool non_matches = ssl_info->peerCertificateSanMatches(non_matching_matcher);
  EXPECT_FALSE(non_matches) << "Non-matching domain should not match";
  ENVOY_LOG_MISC(info, "DNS SAN matcher result for nonexistent.example.com: {}", non_matches);

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

} // namespace Quic
} // namespace Envoy
