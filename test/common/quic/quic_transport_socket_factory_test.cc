#include "source/common/quic/quic_client_transport_socket_factory.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

class QuicServerTransportSocketFactoryConfigTest : public Event::TestUsingSimulatedTime,
                                                   public testing::Test {
public:
  QuicServerTransportSocketFactoryConfigTest()
      : server_api_(Api::createApiForTest(server_stats_store_, simTime())) {
    ON_CALL(context_.server_context_, api()).WillByDefault(ReturnRef(*server_api_));
    ON_CALL(context_.server_context_, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  }

  void verifyQuicServerTransportSocketFactory(std::string yaml, bool expect_early_data) {
    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    Network::DownstreamTransportSocketFactoryPtr transport_socket_factory = THROW_OR_RETURN_VALUE(
        config_factory_.createTransportSocketFactory(proto_config, context_, {}),
        Network::DownstreamTransportSocketFactoryPtr);
    EXPECT_EQ(expect_early_data,
              static_cast<QuicServerTransportSocketFactory&>(*transport_socket_factory)
                  .earlyDataEnabled());
  }

  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  QuicServerTransportSocketConfigFactory config_factory_;
  Stats::TestUtil::TestStore server_stats_store_;
  Api::ApiPtr server_api_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
};

TEST_F(QuicServerTransportSocketFactoryConfigTest, EarlyDataEnabledByDefault) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF");

  verifyQuicServerTransportSocketFactory(yaml, true);
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, EarlyDataExplicitlyDisabled) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
enable_early_data:
  value: false
)EOF");

  verifyQuicServerTransportSocketFactory(yaml, false);
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, EarlyDataExplicitlyEnabled) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
enable_early_data:
  value: true
)EOF");

  verifyQuicServerTransportSocketFactory(yaml, true);
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, ClientAuthSupported) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  require_client_certificate: true
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF");
  // Client authentication should be supported. Verify successful creation.
  verifyQuicServerTransportSocketFactory(yaml, true);
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, TestRequiresClientCertificate) {
  // Test with require_client_certificate = true
  {
    const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  require_client_certificate: true
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
)EOF");

    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    Network::DownstreamTransportSocketFactoryPtr transport_socket_factory = THROW_OR_RETURN_VALUE(
        config_factory_.createTransportSocketFactory(proto_config, context_, {}),
        Network::DownstreamTransportSocketFactoryPtr);

    auto& quic_factory = static_cast<QuicServerTransportSocketFactory&>(*transport_socket_factory);
    EXPECT_TRUE(quic_factory.requiresClientCertificate());
  }

  // Test with require_client_certificate = false
  {
    const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  require_client_certificate: false
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
)EOF");

    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    Network::DownstreamTransportSocketFactoryPtr transport_socket_factory = THROW_OR_RETURN_VALUE(
        config_factory_.createTransportSocketFactory(proto_config, context_, {}),
        Network::DownstreamTransportSocketFactoryPtr);

    auto& quic_factory = static_cast<QuicServerTransportSocketFactory&>(*transport_socket_factory);
    EXPECT_FALSE(quic_factory.requiresClientCertificate());
  }

  // Test with require_client_certificate not specified (default should be false)
  {
    const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
)EOF");

    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    Network::DownstreamTransportSocketFactoryPtr transport_socket_factory = THROW_OR_RETURN_VALUE(
        config_factory_.createTransportSocketFactory(proto_config, context_, {}),
        Network::DownstreamTransportSocketFactoryPtr);

    auto& quic_factory = static_cast<QuicServerTransportSocketFactory&>(*transport_socket_factory);
    EXPECT_FALSE(quic_factory.requiresClientCertificate());
  }
}

class QuicClientTransportSocketFactoryTest : public testing::Test {
public:
  QuicClientTransportSocketFactoryTest() {
    ON_CALL(context_.server_context_, threadLocal()).WillByDefault(ReturnRef(thread_local_));
    EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
        .WillOnce(Return(nullptr));
    EXPECT_CALL(*context_config_, setSecretUpdateCallback(_))
        .WillOnce(testing::SaveArg<0>(&update_callback_));
    factory_ = *Quic::QuicClientTransportSocketFactory::create(
        std::unique_ptr<Envoy::Ssl::ClientContextConfig>(context_config_), context_);
  }

  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  std::unique_ptr<Quic::QuicClientTransportSocketFactory> factory_;
  // Will be owned by factory_.
  NiceMock<Ssl::MockClientContextConfig>* context_config_{
      new NiceMock<Ssl::MockClientContextConfig>};
  std::function<void()> update_callback_;
};

TEST_F(QuicClientTransportSocketFactoryTest, SupportedAlpns) {
  context_config_->alpn_ = "h3,h3-draft29";
  factory_->initialize();
  EXPECT_THAT(factory_->supportedAlpnProtocols(), testing::ElementsAre("h3", "h3-draft29"));
}

TEST_F(QuicClientTransportSocketFactoryTest, GetCryptoConfig) {
  factory_->initialize();
  EXPECT_TRUE(factory_->supportedAlpnProtocols().empty());
  EXPECT_EQ(nullptr, factory_->getCryptoConfig());

  Ssl::ClientContextSharedPtr ssl_context1{new Ssl::MockClientContext()};
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context1));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config1 = factory_->getCryptoConfig();
  EXPECT_NE(nullptr, crypto_config1);

  Ssl::ClientContextSharedPtr ssl_context2{new Ssl::MockClientContext()};
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context2));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config2 = factory_->getCryptoConfig();
  EXPECT_NE(crypto_config2, crypto_config1);
}

// Test for QuicClientCertInitializer functionality
TEST_F(QuicClientTransportSocketFactoryTest, QuicClientCertInitializerTests) {
  // Test initialization with null SSL context
  factory_->initialize();
  EXPECT_EQ(nullptr, factory_->getCryptoConfig());

  // Test with valid SSL context
  Ssl::ClientContextSharedPtr ssl_context{new Ssl::MockClientContext()};
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context));

  // Test the update callback (QuicClientCertInitializer logic)
  update_callback_();

  // Should have created a crypto config
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config = factory_->getCryptoConfig();
  EXPECT_NE(nullptr, crypto_config);

  // Test multiple updates (should create new config each time)
  Ssl::ClientContextSharedPtr ssl_context2{new Ssl::MockClientContext()};
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context2));
  update_callback_();

  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config2 = factory_->getCryptoConfig();
  EXPECT_NE(crypto_config2, crypto_config);
}

// Test for QuicClientCertInitializer error handling.
TEST_F(QuicClientTransportSocketFactoryTest, QuicClientCertInitializerErrorHandling) {
  factory_->initialize();

  // Test with SSL context creation failure.
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(nullptr));

  // Update callback should handle null SSL context gracefully.
  update_callback_();

  // Should not have created a crypto config.
  EXPECT_EQ(nullptr, factory_->getCryptoConfig());
}

// Targeted test to hit the upstream_context_secrets_not_ready statistic.
TEST_F(QuicClientTransportSocketFactoryTest, UpstreamContextSecretsNotReadyStatistic) {
  factory_->initialize();

  // When SSL context is not ready (nullptr in constructor mock), getCryptoConfig should return
  // nullptr This internally triggers the upstream_context_secrets_not_ready statistic increment
  auto crypto_config = factory_->getCryptoConfig();
  EXPECT_EQ(crypto_config, nullptr);

  // Call it again to verify consistent behavior
  auto crypto_config2 = factory_->getCryptoConfig();
  EXPECT_EQ(crypto_config2, nullptr);

  // The statistic is being incremented internally in the implementation
  // This test ensures the code path that increments it is exercised
}

} // namespace Quic
} // namespace Envoy
