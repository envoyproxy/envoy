#include "test/common/tls/cert_validator/timed_cert_validator.h"
#include "test/integration/quic_http_integration_test.h"

namespace Envoy {
namespace Quic {
namespace {

// Integration tests for client certificate authentication (mTLS) on QUIC listeners.
class QuicMtlsIntegrationTest : public QuicHttpIntegrationTestBase,
                                public testing::TestWithParam<Network::Address::IpVersion> {
public:
  QuicMtlsIntegrationTest()
      : QuicHttpIntegrationTestBase(GetParam(), ConfigHelper::quicHttpProxyConfig()) {}

  // Configures the QUIC listener to require and validate client certificates. The test client
  // presents test/config/integration/certs/clientcert.pem by default, which is signed by cacert.
  void setRequireClientCertificate(
      const std::string& trusted_ca = "test/config/integration/certs/cacert.pem",
      const std::string& custom_validator_yaml = "") {
    config_helper_.addConfigModifier(
        [trusted_ca, custom_validator_yaml](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* transport_socket = bootstrap.mutable_static_resources()
                                       ->mutable_listeners(0)
                                       ->mutable_filter_chains(0)
                                       ->mutable_transport_socket();
          auto quic_transport_socket_config = MessageUtil::anyConvert<
              envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport>(
              *transport_socket->mutable_typed_config());
          auto* tls_context = quic_transport_socket_config.mutable_downstream_tls_context();
          tls_context->mutable_require_client_certificate()->set_value(true);
          auto* validation_context =
              tls_context->mutable_common_tls_context()->mutable_validation_context();
          validation_context->mutable_trusted_ca()->set_filename(
              TestEnvironment::runfilesPath(trusted_ca));
          if (!custom_validator_yaml.empty()) {
            TestUtility::loadFromYaml(custom_validator_yaml,
                                      *validation_context->mutable_custom_validator_config());
          }
          std::ignore =
              transport_socket->mutable_typed_config()->PackFrom(quic_transport_socket_config);
        });
  }

  void setForwardClientCertDetails() {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.set_forward_client_cert_details(
              envoy::extensions::filters::network::http_connection_manager::v3::
                  HttpConnectionManager::SANITIZE_SET);
          hcm.mutable_set_current_client_cert_details()->mutable_subject()->set_value(true);
        });
  }

  // Sends a request over the current connection, asserting the client identity is forwarded
  // upstream via XFCC (which is only populated if the server has the peer certificate) and, when
  // expected, that the request was delivered as 0-RTT early data.
  void sendRequestExpectingClientCert(bool expect_early_data) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    waitForNextUpstreamRequest();
    const auto xfcc_header =
        upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
    ASSERT_FALSE(xfcc_header.empty());
    EXPECT_THAT(std::string(xfcc_header[0]->value().getStringView()),
                testing::HasSubstr("Subject="));
    if (expect_early_data) {
      EXPECT_THAT(upstream_request_->headers(),
                  ContainsHeader(Http::Headers::get().EarlyData, "1"));
    }
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
};

INSTANTIATE_TEST_SUITE_P(QuicHttpIntegrationTests, QuicMtlsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// A client presenting a certificate trusted by the server completes the handshake, and the
// certificate details are populated (verified end to end via XFCC forwarding).
TEST_P(QuicMtlsIntegrationTest, MutualTlsHandshakeSuccess) {
  setRequireClientCertificate();
  setForwardClientCertDetails();
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  sendRequestExpectingClientCert(/*expect_early_data=*/false);
}

// Without a client certificate the handshake fails when the listener requires one.
TEST_P(QuicMtlsIntegrationTest, NoClientCertHandshakeFailure) {
  setRequireClientCertificate();
  ssl_client_option_.no_cert_ = true;
  initialize();

  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), std::nullopt);
  // The client finishes its side of the handshake before the server rejects it, so wait for the
  // server-initiated close.
  if (!codec_client_->disconnected()) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  }
}

// A client certificate not signed by the listener's trusted CA fails the handshake.
TEST_P(QuicMtlsIntegrationTest, UntrustedClientCertHandshakeFailure) {
  // The test client's certificate is signed by cacert.pem, not upstreamcacert.pem.
  setRequireClientCertificate("test/config/integration/certs/upstreamcacert.pem");
  initialize();

  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), std::nullopt);
  // The client finishes its side of the handshake before the server rejects it, so wait for the
  // server-initiated close.
  if (!codec_client_->disconnected()) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  }
}

// Client certificate validation using an asynchronous cert validator completes the handshake.
TEST_P(QuicMtlsIntegrationTest, AsyncClientCertValidationSucceeds) {
  auto* cert_validator_factory =
      Registry::FactoryRegistry<Extensions::TransportSockets::Tls::CertValidatorFactory>::
          getFactory("envoy.tls.cert_validator.timed_cert_validator");
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->resetForTest();
  setRequireClientCertificate("test/config/integration/certs/cacert.pem",
                              R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF");
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Filter chains that don't require client certificates keep working without one.
TEST_P(QuicMtlsIntegrationTest, NoClientCertRequiredByDefault) {
  ssl_client_option_.no_cert_ = true;
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// With client certificates required, QUIC does not resume sessions: every connection performs a
// full handshake and re-validates the client certificate. BoringSSL does not resume a session
// that carries a client credential unless the server's client-certificate retention configuration
// matches, which it does not for QUIC, so resumption is effectively disabled on mTLS filter
// chains. This is stricter than TCP TLS (which resumes and carries the identity over), and means
// there is no window where a client identity is attached to a resumed or 0-RTT connection without
// a fresh certificate validation.
TEST_P(QuicMtlsIntegrationTest, MutualTlsDoesNotResume) {
  // All connections share the same PersistentQuicInfoImpl, so a resumable session would be reused.
  concurrency_ = 1;
  setRequireClientCertificate();
  setForwardClientCertDetails();
  configureTlsOptions(/*early_data_enabled=*/true, /*resumption_enabled=*/true);
  initialize();

  // Full handshake on the first connection, with extra round-trips so any session ticket would be
  // received and cached before the connection closes.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  sendRequestExpectingClientCert(/*expect_early_data=*/false);
  sendRequestExpectingClientCert(/*expect_early_data=*/false);
  codec_client_->close();

  // The second connection does another full handshake (no resumption, no 0-RTT) and the client
  // certificate is validated again.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto* quic_session = static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_FALSE(quic_session->IsResumption());
  EXPECT_FALSE(quic_session->EarlyDataAccepted());
  sendRequestExpectingClientCert(/*expect_early_data=*/false);
  codec_client_->close();
}

} // namespace
} // namespace Quic
} // namespace Envoy
