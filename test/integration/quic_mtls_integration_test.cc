#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "source/common/quic/quic_ssl_connection_info.h"

#include "test/integration/quic_http_integration_test.h"
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
    // Configure server to require client certificates
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

      // Set up server certificate
      auto* server_cert = tls_context->mutable_common_tls_context()->add_tls_certificates();
      server_cert->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
      server_cert->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));

      // Set up CA for client cert validation
      tls_context->mutable_common_tls_context()
          ->mutable_validation_context()
          ->mutable_trusted_ca()
          ->set_filename(TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));

      transport_socket->mutable_typed_config()->PackFrom(quic_config);
    });

    // Configure HCM for XFCC headers
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

    QuicHttpIntegrationTestBase::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicMtlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test successful mTLS with client certificate and XFCC header generation
TEST_P(QuicMtlsIntegrationTest, MtlsWithClientCertificateAndXfccHeaders) {
  initialize();

  // Make connection and send request
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Wait for upstream request
  waitForNextUpstreamRequest();

  // Validate that XFCC header is present and populated
  auto xfcc_headers =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  ASSERT_FALSE(xfcc_headers.empty()) << "XFCC header should be present with client certificate";

  std::string xfcc_value = std::string(xfcc_headers[0]->value().getStringView());
  EXPECT_FALSE(xfcc_value.empty()) << "XFCC header should not be empty";

  // Validate specific XFCC fields that our implementation should provide
  EXPECT_THAT(xfcc_value, testing::HasSubstr("Subject=")) << "Should contain certificate subject";

  // Log the XFCC header for verification
  ENVOY_LOG_MISC(info, "XFCC Header: {}", xfcc_value);

  // Send successful response
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Verify SSL connection info is working
  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_NE(quic_session->ssl(), nullptr);
  EXPECT_TRUE(quic_session->ssl()->peerCertificateValidated());

  codec_client_->close();
}

// Test that our QUIC SSL connection info methods work correctly
TEST_P(QuicMtlsIntegrationTest, QuicSslConnectionInfoMethods) {
  initialize();

  // Make connection
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  // Access the QUIC SSL connection info
  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  ASSERT_NE(quic_session->ssl(), nullptr);

  auto ssl_info = quic_session->ssl();

  // Test basic certificate presence
  EXPECT_TRUE(ssl_info->peerCertificatePresented());
  EXPECT_TRUE(ssl_info->peerCertificateValidated());

  // Test certificate digest methods (should not be empty with valid cert)
  EXPECT_FALSE(ssl_info->sha256PeerCertificateDigest().empty());
  EXPECT_FALSE(ssl_info->sha1PeerCertificateDigest().empty());

  // Test certificate subject/issuer methods
  EXPECT_FALSE(ssl_info->subjectPeerCertificate().empty());
  EXPECT_FALSE(ssl_info->issuerPeerCertificate().empty());

  // Test certificate serial number
  EXPECT_FALSE(ssl_info->serialNumberPeerCertificate().empty());

  // Test PEM encoding methods
  EXPECT_FALSE(ssl_info->urlEncodedPemEncodedPeerCertificate().empty());

  ENVOY_LOG_MISC(info, "QUIC SSL Connection Info validation passed");

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  codec_client_->close();
}

} // namespace Quic
} // namespace Envoy
