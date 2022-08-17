#include "default_validator_integration_test.h"

#include <memory>

#include "source/extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {

void SslCertValidatorIntegrationTest::initialize() {
  HttpIntegrationTest::initialize();
  context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
  registerTestServerPorts({"http"});

  test_server_->counter(listenerStatPrefix("ssl.fail_verify_error"))->reset();
  test_server_->counter(listenerStatPrefix("ssl.handshake"))->reset();
}

void SslCertValidatorIntegrationTest::TearDown() {
  HttpIntegrationTest::cleanupUpstreamAndDownstream();
  codec_client_.reset();
  context_manager_.reset();
}

Network::ClientConnectionPtr
SslCertValidatorIntegrationTest::makeSslClientConnection(const ClientSslTransportOptions& options) {
  ClientSslTransportOptions modified_options{options};
  modified_options.setTlsVersion(tls_version_);
  modified_options.setClientWithIntermediateCert(true);

  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  auto client_transport_socket_factory_ptr =
      createClientSslTransportSocketFactory(modified_options, *context_manager_, *api_);
  return dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_transport_socket_factory_ptr->createTransportSocket({}, nullptr), nullptr, nullptr);
}

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientVersions, SslCertValidatorIntegrationTest,
    testing::Combine(
        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
        testing::Values(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2,
                        envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3)),
    SslCertValidatorIntegrationTest::ipClientVersionTestParamsToString);

// Default case, certificate is accepted
TEST_P(SslCertValidatorIntegrationTest, CertValidated) {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(true)
                                  .setTlsV13(true)
                                  .setClientWithIntermediateCert(true));
  initialize();
  auto conn = makeSslClientConnection({});
  IntegrationCodecClientPtr codec = makeHttpConnection(std::move(conn));
  ASSERT_TRUE(codec->connected());
  test_server_->waitForCounterGe(listenerStatPrefix("ssl.handshake"), 1);
  EXPECT_EQ(test_server_->counter(listenerStatPrefix("ssl.fail_verify_error"))->value(), 0);
  codec->close();
}

// With verify-depth set, certificate validation fails
TEST_P(SslCertValidatorIntegrationTest, CertValidationFailed) {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(true)
                                  .setTlsV13(true)
                                  .setClientWithIntermediateCert(true)
                                  .setVerifyDepth(1));
  initialize();
  auto conn = makeSslClientConnection({});
  IntegrationCodecClientPtr codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
  test_server_->waitForCounterGe(listenerStatPrefix("ssl.fail_verify_error"), 1);
  ASSERT_TRUE(codec->waitForDisconnect());
}

} // namespace Ssl
} // namespace Envoy
