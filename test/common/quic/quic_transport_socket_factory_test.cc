#include "source/common/quic/quic_client_transport_socket_factory.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/common/tls/client_context_impl.h"
#include "source/common/tls/context_config_impl.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "quiche/quic/test_tools/test_certificates.h"

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

  void verifyQuicServerTransportSocketFactory(std::string yaml, bool expect_early_data,
                                              bool expect_resumption = true) {
    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    Network::DownstreamTransportSocketFactoryPtr transport_socket_factory = THROW_OR_RETURN_VALUE(
        config_factory_.createTransportSocketFactory(proto_config, context_, {}),
        Network::DownstreamTransportSocketFactoryPtr);
    EXPECT_EQ(expect_early_data,
              static_cast<QuicServerTransportSocketFactory&>(*transport_socket_factory)
                  .earlyDataEnabled());
    EXPECT_EQ(expect_resumption,
              static_cast<QuicServerTransportSocketFactory&>(*transport_socket_factory)
                  .resumptionEnabled());
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

TEST_F(QuicServerTransportSocketFactoryConfigTest, ResumptionExplicitlyDisabled) {
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
enable_resumption:
  value: false
enable_early_data:
  value: false
)EOF");

  verifyQuicServerTransportSocketFactory(yaml, false, false);
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, ResumptionExplicitlyEnabled) {
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
enable_resumption:
  value: true
)EOF");

  verifyQuicServerTransportSocketFactory(yaml, true, true);
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, ResumptionDisabledEarlyDataEnabledInvalid) {
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
enable_resumption:
  value: false
enable_early_data:
  value: true
)EOF");

  EXPECT_THROW_WITH_MESSAGE(
      verifyQuicServerTransportSocketFactory(yaml, true, false), EnvoyException,
      "QUIC early data is enabled but resumption is disabled. Early data requires resumption.");
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, ClientAuthUnsupported) {
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
  EXPECT_THROW_WITH_MESSAGE(verifyQuicServerTransportSocketFactory(yaml, true), EnvoyException,
                            "TLS Client Authentication is not supported over QUIC");
}

// QuicServerTransportSocketFactory implements DownstreamTransportSocketFactory
// only so it can be stored on a FilterChain, not to actually create transport
// sockets — QUIC connections use the QUICHE stack directly via
// EnvoyQuicServerSession. Verify createDownstreamTransportSocket() panics if
// accidentally called.
TEST_F(QuicServerTransportSocketFactoryConfigTest, CreateDownstreamTransportSocketPanics) {
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

  envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  Network::DownstreamTransportSocketFactoryPtr transport_socket_factory = THROW_OR_RETURN_VALUE(
      config_factory_.createTransportSocketFactory(proto_config, context_, {}),
      Network::DownstreamTransportSocketFactoryPtr);
  EXPECT_DEATH(transport_socket_factory->createDownstreamTransportSocket(), "not implemented");
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, GetSessionTicketConfig) {
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

  envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  Network::DownstreamTransportSocketFactoryPtr transport_socket_factory = THROW_OR_RETURN_VALUE(
      config_factory_.createTransportSocketFactory(proto_config, context_, {}),
      Network::DownstreamTransportSocketFactoryPtr);
  auto& quic_factory = static_cast<QuicServerTransportSocketFactory&>(*transport_socket_factory);
  auto config = quic_factory.getSessionTicketConfig();
  // Default config has no session ticket keys and doesn't disable resumption.
  EXPECT_FALSE(config.has_keys);
  EXPECT_FALSE(config.disable_stateless_resumption);
  EXPECT_FALSE(config.handles_session_resumption);
}

class QuicClientTransportSocketFactoryTest : public testing::Test {
public:
  QuicClientTransportSocketFactoryTest() {
    ON_CALL(context_.server_context_, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  }

  void initialize() {
    EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
        .WillOnce(Return(nullptr));
    EXPECT_CALL(*context_config_, setSecretUpdateCallback(_))
        .WillOnce(testing::SaveArg<0>(&update_callback_));
    factory_ = *Quic::QuicClientTransportSocketFactory::create(
        std::unique_ptr<Envoy::Ssl::ClientContextConfig>(context_config_), context_);
  }

  // Builds a real ClientContextImpl, optionally with a TLS certificate, to exercise the client
  // certificate installation on the QUICHE SSL context.
  Ssl::ClientContextSharedPtr makeRealClientContext(bool with_cert) {
    ON_CALL(real_context_config_, cipherSuites())
        .WillByDefault(ReturnRef(
            Extensions::TransportSockets::Tls::ClientContextConfigImpl::DEFAULT_CIPHER_SUITES));
    ON_CALL(real_context_config_, ecdhCurves())
        .WillByDefault(
            ReturnRef(Extensions::TransportSockets::Tls::ClientContextConfigImpl::DEFAULT_CURVES));
    ON_CALL(real_context_config_, alpnProtocols()).WillByDefault(ReturnRef(alpn_));
    ON_CALL(real_context_config_, serverNameIndication()).WillByDefault(ReturnRef(empty_string_));
    ON_CALL(real_context_config_, signatureAlgorithms()).WillByDefault(ReturnRef(sig_algs_));
    if (with_cert) {
      ON_CALL(tls_cert_config_, pkcs12()).WillByDefault(ReturnRef(empty_string_));
      ON_CALL(tls_cert_config_, certificateChainPath()).WillByDefault(ReturnRef(empty_string_));
      ON_CALL(tls_cert_config_, certificateName()).WillByDefault(ReturnRef(empty_string_));
      ON_CALL(tls_cert_config_, privateKeyMethod()).WillByDefault(Return(nullptr));
      ON_CALL(tls_cert_config_, privateKeyPath()).WillByDefault(ReturnRef(empty_string_));
      ON_CALL(tls_cert_config_, password()).WillByDefault(ReturnRef(empty_string_));
      ON_CALL(tls_cert_config_, ocspStaple()).WillByDefault(ReturnRef(ocsp_staple_));
      ON_CALL(tls_cert_config_, certificateChain()).WillByDefault(ReturnRef(test_cert_chain_));
      ON_CALL(tls_cert_config_, privateKey()).WillByDefault(ReturnRef(test_private_key_));
      tls_cert_configs_.emplace_back(tls_cert_config_);
      ON_CALL(real_context_config_, tlsCertificates()).WillByDefault(Return(tls_cert_configs_));
    }
    auto context_or_error = Extensions::TransportSockets::Tls::ClientContextImpl::create(
        *store_.rootScope(), real_context_config_, context_.server_context_);
    THROW_IF_NOT_OK_REF(context_or_error.status());
    return Ssl::ClientContextSharedPtr(std::move(*context_or_error));
  }

  // Declared before factory_ so the store (and its symbol table) outlives the contexts created
  // from it, which the factory retains until destruction.
  Stats::IsolatedStoreImpl store_;
  NiceMock<Ssl::MockClientContextConfig> real_context_config_;
  NiceMock<Ssl::MockTlsCertificateConfig> tls_cert_config_;
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs_;
  const std::string empty_string_;
  const std::string alpn_{"h3"};
  const std::string sig_algs_{"rsa_pss_rsae_sha256"};
  const std::vector<uint8_t> ocsp_staple_;
  const std::string test_cert_chain_{quic::test::kTestCertificateChainPem};
  const std::string test_private_key_{quic::test::kTestCertificatePrivateKeyPem};

  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  std::unique_ptr<Quic::QuicClientTransportSocketFactory> factory_;
  // Will be owned by factory_.
  NiceMock<Ssl::MockClientContextConfig>* context_config_{
      new NiceMock<Ssl::MockClientContextConfig>};
  std::function<void()> update_callback_;
};

TEST_F(QuicClientTransportSocketFactoryTest, SupportedAlpns) {
  initialize();
  context_config_->alpn_ = "h3,h3-draft29";
  factory_->initialize();
  EXPECT_THAT(factory_->supportedAlpnProtocols(), testing::ElementsAre("h3", "h3-draft29"));
}

TEST_F(QuicClientTransportSocketFactoryTest, TlsCertificateSelector) {
  class TestSelector : public Ssl::UpstreamTlsCertificateSelectorFactory {
  public:
    Ssl::UpstreamTlsCertificateSelectorPtr
    createUpstreamTlsCertificateSelector(Ssl::TlsCertificateSelectorContext&) override {
      return nullptr;
    }
    absl::Status onConfigUpdate() override { return absl::OkStatus(); }
  } selector;
  EXPECT_CALL(*context_config_, tlsCertificateSelectorFactory()).WillOnce(Invoke([&]() {
    return makeOptRef(selector);
  }));
  auto factory_or_error = Quic::QuicClientTransportSocketFactory::create(
      std::unique_ptr<Envoy::Ssl::ClientContextConfig>(context_config_), context_);
  EXPECT_FALSE(factory_or_error.ok());
}

TEST_F(QuicClientTransportSocketFactoryTest, GetCryptoConfig) {
  initialize();
  factory_->initialize();
  EXPECT_TRUE(factory_->supportedAlpnProtocols().empty());
  EXPECT_EQ(nullptr, factory_->getCryptoConfig());

  Ssl::ClientContextSharedPtr ssl_context1{new NiceMock<Ssl::MockClientContext>()};
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context1));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config1 = factory_->getCryptoConfig();
  EXPECT_NE(nullptr, crypto_config1);

  Ssl::ClientContextSharedPtr ssl_context2{new NiceMock<Ssl::MockClientContext>()};
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context2));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config2 = factory_->getCryptoConfig();
  EXPECT_NE(crypto_config2, crypto_config1);
}

// A configured client certificate is installed on the QUICHE SSL context so it is presented when
// the upstream requests one.
TEST_F(QuicClientTransportSocketFactoryTest, ClientCertificateConfigured) {
  initialize();
  Ssl::ClientContextSharedPtr context = makeRealClientContext(/*with_cert=*/true);
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(context));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config = factory_->getCryptoConfig();
  ASSERT_NE(nullptr, crypto_config);
  EXPECT_NE(nullptr, SSL_CTX_get0_privatekey(crypto_config->ssl_ctx()));
}

TEST_F(QuicClientTransportSocketFactoryTest, NoClientCertificate) {
  initialize();
  Ssl::ClientContextSharedPtr context = makeRealClientContext(/*with_cert=*/false);
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(context));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config = factory_->getCryptoConfig();
  ASSERT_NE(nullptr, crypto_config);
  EXPECT_EQ(nullptr, SSL_CTX_get0_privatekey(crypto_config->ssl_ctx()));
}

// With the runtime guard disabled, client certificates are not installed (pre-existing behavior).
TEST_F(QuicClientTransportSocketFactoryTest, ClientCertificateRuntimeDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.quic_upstream_client_certificates", "false"}});

  initialize();
  Ssl::ClientContextSharedPtr context = makeRealClientContext(/*with_cert=*/true);
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(context));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config = factory_->getCryptoConfig();
  ASSERT_NE(nullptr, crypto_config);
  EXPECT_EQ(nullptr, SSL_CTX_get0_privatekey(crypto_config->ssl_ctx()));
}

// A client certificate with a private key provider is rejected at config load time because
// QUICHE's client handshaker requires direct access to the private key.
TEST_F(QuicClientTransportSocketFactoryTest, PrivateKeyProviderRejectedAtConfigLoad) {
  // This test does not pass the fixture's context_config_ to a factory; take ownership so the
  // mock is deleted.
  std::unique_ptr<Ssl::MockClientContextConfig> unused_config{context_config_};
  auto config = std::make_unique<NiceMock<Ssl::MockClientContextConfig>>();
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  auto provider = std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  ON_CALL(cert_config, privateKeyMethod()).WillByDefault(Return(provider));
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> certs{cert_config};
  ON_CALL(*config, tlsCertificates()).WillByDefault(Return(certs));

  auto factory_or_error =
      Quic::QuicClientTransportSocketFactory::create(std::move(config), context_);
  EXPECT_FALSE(factory_or_error.ok());
  EXPECT_EQ(factory_or_error.status().code(), absl::StatusCode::kUnimplemented);
}

// With the runtime guard disabled, a private key provider does not fail config load (pre-existing
// behavior: the certificate is simply not sent over QUIC).
TEST_F(QuicClientTransportSocketFactoryTest, PrivateKeyProviderAllowedWhenRuntimeDisabled) {
  // This test does not pass the fixture's context_config_ to a factory; take ownership so the
  // mock is deleted.
  std::unique_ptr<Ssl::MockClientContextConfig> unused_config{context_config_};
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.quic_upstream_client_certificates", "false"}});

  auto config = std::make_unique<NiceMock<Ssl::MockClientContextConfig>>();
  NiceMock<Ssl::MockTlsCertificateConfig> cert_config;
  auto provider = std::make_shared<NiceMock<Ssl::MockPrivateKeyMethodProvider>>();
  ON_CALL(cert_config, privateKeyMethod()).WillByDefault(Return(provider));
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> certs{cert_config};
  ON_CALL(*config, tlsCertificates()).WillByDefault(Return(certs));
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(nullptr));

  auto factory_or_error =
      Quic::QuicClientTransportSocketFactory::create(std::move(config), context_);
  EXPECT_TRUE(factory_or_error.ok());
}

// If a certificate which cannot be installed on the QUICHE SSL context arrives at runtime (via
// SDS), the factory fails closed: no crypto config is returned, so no connections are created
// without the configured client certificate.
TEST_F(QuicClientTransportSocketFactoryTest, FailClosedWhenCertificateCannotBeInstalled) {
  initialize();

  // A TlsContext with a certificate chain but no directly accessible private key, as is the case
  // when the certificate uses a private key provider.
  Ssl::TlsContext tls_context;
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(test_cert_chain_.data(), test_cert_chain_.size()));
  tls_context.cert_chain_.reset(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  ASSERT_NE(nullptr, tls_context.cert_chain_);
  tls_context.ssl_ctx_.reset(SSL_CTX_new(TLS_method()));

  auto* mock_context = new NiceMock<Ssl::MockClientContext>();
  Ssl::ClientContextSharedPtr ssl_context{mock_context};
  ON_CALL(*mock_context, getTlsContext()).WillByDefault(ReturnRef(tls_context));
  EXPECT_CALL(context_.server_context_.ssl_context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context));
  update_callback_();

  EXPECT_EQ(nullptr, factory_->getCryptoConfig());
  // The failure is not cached; subsequent calls fail the same way.
  EXPECT_EQ(nullptr, factory_->getCryptoConfig());
}

} // namespace Quic
} // namespace Envoy
