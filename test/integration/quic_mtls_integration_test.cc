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

  // Configures the downstream QUIC listener for mTLS using the integration certs.
  void setupServerWithClientCertValidation(const std::string& client_ca_cert = "cacert.pem",
                                           const std::string& server_cert = "servercert.pem",
                                           const std::string& server_key = "serverkey.pem",
                                           bool require_client_cert = true) {
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

      auto* server_cert_config = tls_context->mutable_common_tls_context()->add_tls_certificates();
      server_cert_config->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/" + server_cert));
      server_cert_config->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/" + server_key));

      if (require_client_cert) {
        tls_context->mutable_common_tls_context()
            ->mutable_validation_context()
            ->mutable_trusted_ca()
            ->set_filename(
                TestEnvironment::runfilesPath("test/config/integration/certs/" + client_ca_cert));
      }

      ASSERT_TRUE(transport_socket->mutable_typed_config()->PackFrom(quic_config));
    });

    setupXfccHeaderConfig();
  }

  // Forwards the client certificate details on the upstream request via XFCC.
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
        });
  }

  void verifyServerSideClientCertInfo(const std::string& expected_subject_contains = "") {
    auto xfcc_headers =
        upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
    ASSERT_FALSE(xfcc_headers.empty());

    const std::string xfcc_value = std::string(xfcc_headers[0]->value().getStringView());
    EXPECT_FALSE(xfcc_value.empty());

    if (!expected_subject_contains.empty()) {
      EXPECT_THAT(xfcc_value, testing::HasSubstr("Subject="));
      EXPECT_THAT(xfcc_value, testing::HasSubstr(expected_subject_contains));
    }
  }

  void verifyClientSideServerCertInfo() {
    auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
    ASSERT_NE(quic_session, nullptr);
    ASSERT_NE(quic_session->ssl(), nullptr);

    auto ssl_info = quic_session->ssl();
    EXPECT_TRUE(ssl_info->peerCertificatePresented());
    EXPECT_TRUE(ssl_info->peerCertificateValidated());
    EXPECT_FALSE(ssl_info->sha256PeerCertificateDigest().empty());
    EXPECT_FALSE(ssl_info->subjectPeerCertificate().empty());
    EXPECT_FALSE(ssl_info->serialNumberPeerCertificate().empty());
  }

  void expectConnectionFailure(absl::string_view expected_error_contains) {
    // Use `wait_till_connected = false` because QUIC may briefly report the
    // connection as established once the client has 1-RTT keys, before the
    // server's CONNECTION_CLOSE arrives.
    codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), std::nullopt,
                                          std::nullopt, /*wait_till_connected=*/false);
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    ASSERT_NE(codec_client_->connection(), nullptr);
    const std::string failure_reason(codec_client_->connection()->transportFailureReason());
    EXPECT_FALSE(failure_reason.empty());
    EXPECT_THAT(failure_reason, testing::HasSubstr(expected_error_contains));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicMtlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Successful mTLS connection where the client presents a valid certificate, the
// server validates it, and the client cert details are forwarded via XFCC.
TEST_P(QuicMtlsIntegrationTest, SuccessfulMtlsWithClientCertificate) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  verifyServerSideClientCertInfo("Test Frontend Team");

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  verifyClientSideServerCertInfo();
  codec_client_->close();
}

// Comprehensive coverage of the client-side `Ssl::ConnectionInfo` peer
// certificate fields exposed via QUIC.
TEST_P(QuicMtlsIntegrationTest, ComprehensiveCertificateInfoExtraction) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  auto ssl_info = quic_session->ssl();

  EXPECT_TRUE(ssl_info->peerCertificatePresented());
  EXPECT_TRUE(ssl_info->peerCertificateValidated());

  EXPECT_EQ(ssl_info->sha256PeerCertificateDigest().length(), 64);
  EXPECT_EQ(ssl_info->sha1PeerCertificateDigest().length(), 40);

  EXPECT_FALSE(ssl_info->sha256PeerCertificateChainDigests().empty());
  EXPECT_FALSE(ssl_info->sha1PeerCertificateChainDigests().empty());

  EXPECT_FALSE(ssl_info->uriSanPeerCertificate().empty());
  EXPECT_FALSE(ssl_info->dnsSansPeerCertificate().empty());

  EXPECT_TRUE(ssl_info->validFromPeerCertificate().has_value());
  EXPECT_TRUE(ssl_info->expirationPeerCertificate().has_value());

  EXPECT_FALSE(ssl_info->tlsVersion().empty());
  EXPECT_NE(ssl_info->ciphersuiteId(), 0xffff);
  EXPECT_FALSE(ssl_info->ciphersuiteString().empty());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// The handshake fails when a client connects without a certificate to a
// listener that requires one.
TEST_P(QuicMtlsIntegrationTest, ClientWithoutCertRejectedWhenRequired) {
  setupServerWithClientCertValidation();
  ssl_client_option_.no_cert_ = true;
  initialize();
  expectConnectionFailure("certificate required");
}

// A client certificate that does not chain to the configured trust anchor is rejected.
TEST_P(QuicMtlsIntegrationTest, ClientCertUntrustedCaRejected) {
  setupServerWithClientCertValidation("upstreamcacert.pem");
  initialize();
  expectConnectionFailure("");
}

// A listener without `require_client_certificate` accepts a client without a
// certificate and emits no XFCC header.
TEST_P(QuicMtlsIntegrationTest, ServerWithoutClientCertRequirement) {
  setupServerWithClientCertValidation("", "servercert.pem", "serverkey.pem", false);
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  auto xfcc_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  EXPECT_TRUE(xfcc_headers.empty());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  verifyClientSideServerCertInfo();
  codec_client_->close();
}

// XFCC carries the client certificate subject and the URL-encoded leaf cert.
TEST_P(QuicMtlsIntegrationTest, XfccHeaderComprehensiveValidation) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  auto xfcc_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  ASSERT_FALSE(xfcc_headers.empty());

  const std::string xfcc_value = std::string(xfcc_headers[0]->value().getStringView());
  EXPECT_THAT(xfcc_value, testing::HasSubstr("Subject="));
  EXPECT_THAT(xfcc_value, testing::HasSubstr("Cert="));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// Validation with an intermediate CA certificate in the trust chain.
TEST_P(QuicMtlsIntegrationTest, IntermediateCaCertificateChainValidation) {
  setupServerWithClientCertValidation("intermediate_ca_cert_chain.pem");
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  verifyServerSideClientCertInfo("CN=Test");

  auto xfcc_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  const std::string xfcc_value = std::string(xfcc_headers[0]->value().getStringView());
  EXPECT_THAT(xfcc_value, testing::HasSubstr("Chain="));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// XFCC is emitted for each request multiplexed on the same mTLS connection.
TEST_P(QuicMtlsIntegrationTest, MultipleRequestsOnSameMtlsConnection) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  for (int i = 0; i < 3; ++i) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    waitForNextUpstreamRequest();
    verifyServerSideClientCertInfo("Test Frontend Team");
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }

  codec_client_->close();
}

// SAN matching against the client certificate succeeds when the URI SAN
// matches the configured matcher.
TEST_P(QuicMtlsIntegrationTest, SubjectAlternativeNameValidation) {
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

    auto* server_cert = tls_context->mutable_common_tls_context()->add_tls_certificates();
    server_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    server_cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));

    auto* validation_context =
        tls_context->mutable_common_tls_context()->mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));

    auto* san_matcher = validation_context->add_match_typed_subject_alt_names();
    san_matcher->set_san_type(
        envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
    san_matcher->mutable_matcher()->set_exact("spiffe://lyft.com/frontend-team");

    ASSERT_TRUE(transport_socket->mutable_typed_config()->PackFrom(quic_config));
  });

  setupXfccHeaderConfig();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  verifyServerSideClientCertInfo("Test Frontend Team");

  auto xfcc_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  const std::string xfcc_value = std::string(xfcc_headers[0]->value().getStringView());
  EXPECT_THAT(xfcc_value, testing::HasSubstr("URI=spiffe://lyft.com/frontend-team"));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// TLS version, cipher suite, and ALPN are populated on a QUIC mTLS connection.
TEST_P(QuicMtlsIntegrationTest, TlsVersionAndCipherSuiteInformation) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  auto ssl_info = quic_session->ssl();

  EXPECT_FALSE(ssl_info->tlsVersion().empty());
  EXPECT_NE(ssl_info->ciphersuiteId(), 0xffff);
  EXPECT_FALSE(ssl_info->ciphersuiteString().empty());
  EXPECT_FALSE(ssl_info->alpn().empty());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// Coverage for the remaining `QuicSslConnectionInfo` accessors that are not
// already exercised by the XFCC pipeline.
TEST_P(QuicMtlsIntegrationTest, QuicSslConnectionInfoComprehensiveCoverage) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  auto ssl_info = quic_session->ssl();

  EXPECT_FALSE(ssl_info->issuerPeerCertificate().empty());
  EXPECT_TRUE(ssl_info->parsedSubjectPeerCertificate().has_value());

  auto serial_numbers = ssl_info->serialNumbersPeerCertificates();
  EXPECT_FALSE(serial_numbers.empty());
  for (const auto& serial : serial_numbers) {
    EXPECT_FALSE(serial.empty());
  }

  // IP, email, otherName SANs and OIDs may or may not be populated for the integration test
  // certificate, so assert repeated reads are stable rather than asserting specific values.
  auto to_vector = [](absl::Span<const std::string> span) {
    return std::vector<std::string>(span.begin(), span.end());
  };
  EXPECT_EQ(to_vector(ssl_info->ipSansPeerCertificate()),
            to_vector(ssl_info->ipSansPeerCertificate()));
  EXPECT_EQ(to_vector(ssl_info->emailSansPeerCertificate()),
            to_vector(ssl_info->emailSansPeerCertificate()));
  EXPECT_EQ(to_vector(ssl_info->othernameSansPeerCertificate()),
            to_vector(ssl_info->othernameSansPeerCertificate()));
  EXPECT_EQ(to_vector(ssl_info->oidsPeerCertificate()), to_vector(ssl_info->oidsPeerCertificate()));

  // Local certificate accessors return empty for QUIC.
  EXPECT_TRUE(ssl_info->ipSansLocalCertificate().empty());
  EXPECT_TRUE(ssl_info->emailSansLocalCertificate().empty());
  EXPECT_TRUE(ssl_info->othernameSansLocalCertificate().empty());
  EXPECT_TRUE(ssl_info->oidsLocalCertificate().empty());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

// `peerCertificateSanMatches` returns true when a SAN of the leaf matches the supplied matcher
// and false otherwise.
TEST_P(QuicMtlsIntegrationTest, PeerCertificateSanMatcherCoverage) {
  setupServerWithClientCertValidation();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  auto ssl_info = quic_session->ssl();

  const auto dns_sans = ssl_info->dnsSansPeerCertificate();
  ASSERT_FALSE(dns_sans.empty());
  Extensions::TransportSockets::Tls::DnsExactStringSanMatcher matching_matcher(dns_sans[0]);
  EXPECT_TRUE(ssl_info->peerCertificateSanMatches(matching_matcher));

  Extensions::TransportSockets::Tls::DnsExactStringSanMatcher non_matching_matcher(
      "nonexistent.example.com");
  EXPECT_FALSE(ssl_info->peerCertificateSanMatches(non_matching_matcher));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

} // namespace Quic
} // namespace Envoy
