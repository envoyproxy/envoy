#include "spiffe_validator_integration_test.h"

#include <memory>

#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {

void SslSPIFFECertValidatorIntegrationTest::initialize() {
  config_helper_.addSslConfig(
      ConfigHelper::ServerSslOptions().setRsaCert(true).setTlsV13(true).setCustomValidatorConfig(
          custom_validator_config_));
  HttpIntegrationTest::initialize();

  context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(timeSystem());
  registerTestServerPorts({"http"});
}

void SslSPIFFECertValidatorIntegrationTest::TearDown() {
  HttpIntegrationTest::cleanupUpstreamAndDownstream();
  codec_client_.reset();
  context_manager_.reset();
}

Network::ClientConnectionPtr SslSPIFFECertValidatorIntegrationTest::makeSslClientConnection(
    const ClientSslTransportOptions& options) {
  ClientSslTransportOptions modified_options{options};
  modified_options.setTlsVersion(tls_version_);

  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  auto client_transport_socket_factory_ptr =
      createClientSslTransportSocketFactory(modified_options, *context_manager_, *api_);
  return dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_transport_socket_factory_ptr->createTransportSocket({}), nullptr);
}

void SslSPIFFECertValidatorIntegrationTest::checkVerifyErrorCouter(uint64_t value) {
  Stats::CounterSharedPtr counter =
      test_server_->counter(listenerStatPrefix("ssl.fail_verify_error"));
  EXPECT_EQ(value, counter->value());
  counter->reset();
}

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientVersions, SslSPIFFECertValidatorIntegrationTest,
    testing::Combine(
        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
        testing::Values(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2,
                        envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3)),
    SslSPIFFECertValidatorIntegrationTest::ipClientVersionTestParamsToString);

// clientcert.pem's san is "spiffe://lyft.com/frontend-team" so it should be accepted.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorAccepted) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            *typed_conf);

  custom_validator_config_ = typed_conf;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkVerifyErrorCouter(0);
}

// clientcert.pem's san is "spiffe://lyft.com/frontend-team" so it should be rejected.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorRejected1) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  initialize();
  auto conn = makeSslClientConnection({});
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    auto codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
    EXPECT_FALSE(codec->connected());
  } else {
    auto codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
  checkVerifyErrorCouter(1);
}

// clientcert.pem's san is "spiffe://lyft.com/frontend-team" but the corresponding trust bundle does
// not match with the client cert. So this should also be rejected.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorRejected2) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/fake_ca_cert.pem"
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  initialize();
  auto conn = makeSslClientConnection({});
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    auto codec = makeRawHttpConnection(std::move(conn), absl::nullopt);
    EXPECT_FALSE(codec->connected());
  } else {
    auto codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
  checkVerifyErrorCouter(1);
}

} // namespace Ssl
} // namespace Envoy
