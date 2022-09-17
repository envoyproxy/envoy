#include "spiffe_validator_integration_test.h"

#include <memory>

#include "source/extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {

void SslSPIFFECertValidatorIntegrationTest::initialize() {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(true)
                                  .setTlsV13(true)
                                  .setRsaCertOcspStaple(false)
                                  .setCustomValidatorConfig(custom_validator_config_)
                                  .setSanMatchers(san_matchers_)
                                  .setAllowExpiredCertificate(allow_expired_cert_));
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
    const ClientSslTransportOptions& options, bool use_expired = false) {
  ClientSslTransportOptions modified_options{options};
  modified_options.setTlsVersion(tls_version_);
  modified_options.use_expired_spiffe_cert_ = use_expired;

  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  auto client_transport_socket_factory_ptr =
      createClientSslTransportSocketFactory(modified_options, *context_manager_, *api_);
  return dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_transport_socket_factory_ptr->createTransportSocket({}, nullptr), nullptr, nullptr);
}

void SslSPIFFECertValidatorIntegrationTest::checkVerifyErrorCouter(uint64_t value) {
  Stats::CounterSharedPtr counter =
      test_server_->counter(listenerStatPrefix("ssl.fail_verify_error"));
  EXPECT_EQ(value, counter->value());
  counter->reset();
}

void SslSPIFFECertValidatorIntegrationTest::addStringMatcher(
    const envoy::type::matcher::v3::StringMatcher& matcher) {
  san_matchers_.emplace_back();
  *san_matchers_.back().mutable_matcher() = matcher;
  san_matchers_.back().set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
  san_matchers_.emplace_back();
  *san_matchers_.back().mutable_matcher() = matcher;
  san_matchers_.back().set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
  san_matchers_.emplace_back();
  *san_matchers_.back().mutable_matcher() = matcher;
  san_matchers_.back().set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::EMAIL);
  san_matchers_.emplace_back();
  *san_matchers_.back().mutable_matcher() = matcher;
  san_matchers_.back().set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::IP_ADDRESS);
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

// Client certificate has expired but the config allows expired certificates, so this case should
// be accepted.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorExpiredButAccepcepted) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  allow_expired_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    const bool use_expired_certificate = true;
    return makeSslClientConnection({}, use_expired_certificate);
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkVerifyErrorCouter(0);
}

// clientcert.pem has "spiffe://lyft.com/frontend-team" URI SAN, so this case should be accepted.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorSANMatch) {
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

  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_prefix("spiffe://lyft.com/");
  addStringMatcher(matcher);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkVerifyErrorCouter(0);
}

// clientcert.pem has "spiffe://lyft.com/frontend-team" URI SAN, so this case should be rejected.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorSANNotMatch) {
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

  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_prefix("spiffe://example.com/");
  // The cert has "DNS.1 = lyft.com" but SPIFFE validator must ignore SAN types other than URI.
  matcher.set_prefix("www.lyft.com");
  addStringMatcher(matcher);
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
  Stats::CounterSharedPtr counter =
      test_server_->counter(listenerStatPrefix("ssl.fail_verify_san"));
  EXPECT_EQ(1u, counter->value());
  counter->reset();
}

// Client certificate has expired and the config does NOT allow expired certificates, so this case
// should be rejected.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorExpiredAndRejected) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  // Explicitly specify "false" just in case and for clarity.
  allow_expired_cert_ = false;
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

// clientcert.pem's san is "spiffe://lyft.com/frontend-team" but the corresponding trust bundle
// does not match with the client cert. So this should also be rejected.
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
