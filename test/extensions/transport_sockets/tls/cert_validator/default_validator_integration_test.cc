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
      client_transport_socket_factory_ptr->createTransportSocket({}), nullptr);
}

void SslCertValidatorIntegrationTest::checkVerifyErrorCouter(uint64_t value) {
  Stats::CounterSharedPtr counter =
      test_server_->counter(listenerStatPrefix("ssl.fail_verify_error"));
  EXPECT_EQ(value, counter->value());
  counter->reset();
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
  IntegrationCodecClientPtr codec;
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
  } else {
    codec = makeHttpConnection(std::move(conn));
  }
  ASSERT_TRUE(codec->connected());
  checkVerifyErrorCouter(0);
  codec->close();
}

// With verify-depth set, certificate validation fails
TEST_P(SslCertValidatorIntegrationTest, CertValidationFailed) {
  absl::optional<uint32_t> depth(1);
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(true)
                                  .setTlsV13(true)
                                  .setClientWithIntermediateCert(true)
                                  .setVerifyDepth(depth));
  initialize();
  auto conn = makeSslClientConnection({});
  IntegrationCodecClientPtr codec;
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
    ASSERT_FALSE(codec->connected());
  } else {
    codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
  checkVerifyErrorCouter(1);
}

} // namespace Ssl
} // namespace Envoy
