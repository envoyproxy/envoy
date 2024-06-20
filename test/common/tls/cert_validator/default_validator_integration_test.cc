#include "default_validator_integration_test.h"

#include <memory>

#include "source/common/tls/context_manager_impl.h"

#include "test/integration/integration.h"
#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {

void SslCertValidatorIntegrationTest::initialize() {
  HttpIntegrationTest::initialize();
  context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
      factory_context_.serverFactoryContext());
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

// Test Config:
//   peer certificate chain: leaf cert -> level 2 intermediate -> level 1 intermediate -> root
//   trust ca certificate chain: level-2 intermediate -> level-1 intermediate
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

// Test Config:
//   peer certificate chain: leaf cert -> level-2 intermediate -> level-1 intermediate -> root
//   trust ca certificate chain: level-2 intermediate -> level-1 intermediate
// With verify-depth set, certificate validation succeeds since we allow partial chain
TEST_P(SslCertValidatorIntegrationTest, CertValidatedWithVerifyDepth) {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(true)
                                  .setTlsV13(true)
                                  .setClientWithIntermediateCert(true)
                                  .setVerifyDepth(1));
  initialize();
  auto conn = makeSslClientConnection({});
  IntegrationCodecClientPtr codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
  ASSERT_TRUE(codec->connected());
  test_server_->waitForCounterGe(listenerStatPrefix("ssl.handshake"), 1);
  EXPECT_EQ(test_server_->counter(listenerStatPrefix("ssl.fail_verify_error"))->value(), 0);
  codec->close();
}

// Test Config:
//   peer certificate chain: leaf cert -> level-2 intermediate -> level-1 intermediate -> root
//   trust ca certificate chain: root
// With only root trusted, certificate validation succeeds without setting max depth since the
// default value is 100
TEST_P(SslCertValidatorIntegrationTest, CertValidationSucceedNoDepthWithTrustRootOnly) {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(true)
                                  .setTlsV13(true)
                                  .setClientWithIntermediateCert(true)
                                  .setTrustRootOnly(true));
  initialize();
  auto conn = makeSslClientConnection({});
  IntegrationCodecClientPtr codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
  ASSERT_TRUE(codec->connected());
  test_server_->waitForCounterGe(listenerStatPrefix("ssl.handshake"), 1);
  EXPECT_EQ(test_server_->counter(listenerStatPrefix("ssl.fail_verify_error"))->value(), 0);
  codec->close();
}

// Test Config:
//   peer certificate chain: leaf cert -> level-2 intermediate -> level-1 intermediate -> root
//   trust ca certificate chain: root
// With only root trusted, certificate validation succeeds because root ca is in depth 3 and max
// depth is 3
TEST_P(SslCertValidatorIntegrationTest, CertValidationSucceedDepthWithTrustRootOnly) {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(true)
                                  .setTlsV13(true)
                                  .setClientWithIntermediateCert(true)
                                  .setTrustRootOnly(true)
                                  .setVerifyDepth(3));
  initialize();
  auto conn = makeSslClientConnection({});
  IntegrationCodecClientPtr codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
  ASSERT_TRUE(codec->connected());
  test_server_->waitForCounterGe(listenerStatPrefix("ssl.handshake"), 1);
  EXPECT_EQ(test_server_->counter(listenerStatPrefix("ssl.fail_verify_error"))->value(), 0);
  codec->close();
}

// Test Config:
//   peer certificate chain: leaf cert -> level-2 intermediate -> level-1 intermediate -> root
//   trust ca certificate chain: root
// With only root ca trusted, certificate validation is expected to fail because root ca is in depth
// 3 while max depth is 2
TEST_P(SslCertValidatorIntegrationTest, CertValidationFailedDepthWithTrustRootOnly) {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(true)
                                  .setTlsV13(true)
                                  .setClientWithIntermediateCert(true)
                                  .setTrustRootOnly(true)
                                  .setVerifyDepth(2));
  initialize();
  auto conn = makeSslClientConnection({});
  IntegrationCodecClientPtr codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
  test_server_->waitForCounterGe(listenerStatPrefix("ssl.fail_verify_error"), 1);
  ASSERT_TRUE(codec->waitForDisconnect());
}

} // namespace Ssl
} // namespace Envoy
