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
    config_helper_.addConfigModifier([trusted_ca, custom_validator_yaml](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
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
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  const auto xfcc_header =
      upstream_request_->headers().get(Http::LowerCaseString("x-forwarded-client-cert"));
  ASSERT_FALSE(xfcc_header.empty());
  // The client certificate hash and subject are extracted from the presented certificate.
  EXPECT_THAT(std::string(xfcc_header[0]->value().getStringView()),
              testing::AllOf(testing::HasSubstr("Hash="), testing::HasSubstr("Subject=")));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
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

} // namespace
} // namespace Quic
} // namespace Envoy
