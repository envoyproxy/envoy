#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.h"
#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.validate.h"

#include "source/extensions/filters/network/mtls_failure_response/config.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MtlsFailureResponse {

namespace {

class MtlsFailureResponseIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  MtlsFailureResponseIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      config_helper_.configDownstreamTransportSocketWithTls(
          bootstrap,
          [](envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
            common_tls_context.add_tls_certificates()->mutable_certificate_chain()->set_filename(
                TestEnvironment::substitute(
                    "{{ test_rundir }}/test/config/integration/certs/servercert.pem"));
            common_tls_context.mutable_tls_certificates(0)->mutable_private_key()->set_filename(
                TestEnvironment::substitute(
                    "{{ test_rundir }}/test/config/integration/certs/serverkey.pem"));
            common_tls_context.mutable_validation_context()->mutable_trusted_ca()->set_filename(
                TestEnvironment::substitute("{{ test_rundir }}"
                                            "/test/config/integration/certs/"
                                            "cacert.pem"));
            common_tls_context.mutable_validation_context()->set_trust_chain_verification(
                envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
                    ACCEPT_UNTRUSTED);
          },
          false);
    });

    HttpIntegrationTest::initialize();

    context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
        factory_context_.serverFactoryContext());
    registerTestServerPorts({"http"});
  }

  Network::ClientConnectionPtr
  makeSslClientConnection(const Ssl::ClientSslTransportOptions& options) {
    Ssl::ClientSslTransportOptions modified_options{options};

    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("http"));
    auto client_transport_socket_factory_ptr =
        createClientSslTransportSocketFactory(modified_options, *context_manager_, *api_);
    return dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        client_transport_socket_factory_ptr->createTransportSocket({}, nullptr), nullptr, nullptr);
  }

protected:
  std::unique_ptr<Ssl::ContextManager> context_manager_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MtlsFailureResponseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test if the filter closes the connection when the client certificate is not presented
TEST_P(MtlsFailureResponseIntegrationTest,
       InvalidClientCertificateWithCloseConnectionOnPresentationFailure) {

  config_helper_.addNetworkFilter(
      R"EOF(
name: envoy.filters.network.mtls_failure_response
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.mtls_failure_response.v3.MtlsFailureResponse
  validation_mode: PRESENTED
  failure_mode: CLOSE_CONNECTION
)EOF");

  initialize();

  auto conn = makeSslClientConnection({Ssl::ClientSslTransportOptions().setClientWithNoCert(true)});
  IntegrationCodecClientPtr codec = makeHttpConnection(std::move(conn));
  ASSERT_TRUE(codec->waitForDisconnect(std::chrono::milliseconds(500)));
  codec->close();
}

// Test if the filter keeps the connection open when the client certificate is presented
TEST_P(MtlsFailureResponseIntegrationTest,
       ValidClientCertificateWithCloseConnectionOnPresentationFailure) {

  config_helper_.addNetworkFilter(
      R"EOF(
name: envoy.filters.network.mtls_failure_response
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.mtls_failure_response.v3.MtlsFailureResponse
  validation_mode: PRESENTED
  failure_mode: CLOSE_CONNECTION
)EOF");

  initialize();

  auto conn = makeSslClientConnection({});
  IntegrationCodecClientPtr codec = makeHttpConnection(std::move(conn));
  ASSERT_TRUE(codec->connected());
  ASSERT_FALSE(codec->waitForDisconnect(std::chrono::milliseconds(500)));
  codec->close();
}

// Test if the filter closes the connection when the client certificate is invalid
TEST_P(MtlsFailureResponseIntegrationTest,
       InvalidClientCertificateWithCloseConnectionOnValidationFailure) {

  config_helper_.addNetworkFilter(
      R"EOF(
name: envoy.filters.network.mtls_failure_response
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.mtls_failure_response.v3.MtlsFailureResponse
  validation_mode: VALIDATED
  failure_mode: CLOSE_CONNECTION
)EOF");

  initialize();
  auto conn =
      makeSslClientConnection({Ssl::ClientSslTransportOptions().setUseExpiredSpiffeCer(true)});
  IntegrationCodecClientPtr codec = makeHttpConnection(std::move(conn));
  ASSERT_TRUE(codec->waitForDisconnect(std::chrono::milliseconds(500)));
  codec->close();
}

// Test if the filter continues the connection when the client certificate is valid
TEST_P(MtlsFailureResponseIntegrationTest,
       ValidClientCertificateWithCloseConnectionOnValidationFailure) {

  config_helper_.addNetworkFilter(
      R"EOF(
name: envoy.filters.network.mtls_failure_response
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.mtls_failure_response.v3.MtlsFailureResponse
  validation_mode: VALIDATED
  failure_mode: CLOSE_CONNECTION
)EOF");

  initialize();

  auto conn = makeSslClientConnection({});
  IntegrationCodecClientPtr codec = makeHttpConnection(std::move(conn));
  ASSERT_TRUE(codec->connected());
  ASSERT_FALSE(codec->waitForDisconnect(std::chrono::milliseconds(500)));
  codec->close();
}

// Test if the filter responds back when the client certificate is valid
TEST_P(MtlsFailureResponseIntegrationTest,
       ValidClientCertificateWithKeepConnectionOpenOnValidationFailure) {

  config_helper_.addNetworkFilter(
      R"EOF(
name: envoy.filters.network.mtls_failure_response
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.mtls_failure_response.v3.MtlsFailureResponse
  validation_mode: VALIDATED
  failure_mode: KEEP_CONNECTION_OPEN
)EOF");

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };

  // Test the router request and response with the body sizes
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// Test if the filter fails to respond back when the client certificate is invalid
TEST_P(MtlsFailureResponseIntegrationTest,
       InvalidClientCertificateWithKeepConnectionOpenOnValidationFailure) {

  config_helper_.addNetworkFilter(
      R"EOF(
name: envoy.filters.network.mtls_failure_response
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.mtls_failure_response.v3.MtlsFailureResponse
  validation_mode: VALIDATED
  failure_mode: KEEP_CONNECTION_OPEN
)EOF");

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({Ssl::ClientSslTransportOptions().setUseExpiredSpiffeCer(true)});
  };

  // Test the router request and response with the body sizes
  EXPECT_DEATH(testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator),
               "Timed out waiting for new connection.");
}

// Test if the filter fails to respond back when the client certificate is invalid
TEST_P(MtlsFailureResponseIntegrationTest,
       MultipleInvalidClientCertificateConnectionsWithKeepConnectionOpenOnValidationFailure) {

  config_helper_.addNetworkFilter(
      R"EOF(
name: envoy.filters.network.mtls_failure_response
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.mtls_failure_response.v3.MtlsFailureResponse
  validation_mode: VALIDATED
  failure_mode: KEEP_CONNECTION_OPEN
  token_bucket:
    max_tokens: 1
    tokens_per_fill: 1
    fill_interval: 60s
)EOF");

  initialize();

  auto conn1 =
      makeSslClientConnection({Ssl::ClientSslTransportOptions().setUseExpiredSpiffeCer(true)});
  IntegrationCodecClientPtr codec1 = makeHttpConnection(std::move(conn1));
  ASSERT_TRUE(codec1->connected());

  auto conn2 =
      makeSslClientConnection({Ssl::ClientSslTransportOptions().setUseExpiredSpiffeCer(true)});
  IntegrationCodecClientPtr codec2 = makeHttpConnection(std::move(conn2));
  ASSERT_TRUE(codec2->disconnected());
  codec1->close();
  codec2->close();
}

} // namespace
} // namespace MtlsFailureResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
