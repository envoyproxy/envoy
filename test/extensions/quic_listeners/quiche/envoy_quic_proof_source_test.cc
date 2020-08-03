#include <memory>
#include <string>
#include <vector>

#include "extensions/quic_listeners/quiche/envoy_quic_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_proof_verifier.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/transport_sockets/tls/context_config_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/test_tools/test_certificates.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {

namespace Quic {

class TestGetProofCallback : public quic::ProofSource::Callback {
public:
  TestGetProofCallback(bool& called, bool should_succeed, const std::string& server_config,
                       quic::QuicTransportVersion& version, quiche::QuicheStringPiece chlo_hash,
                       Network::FilterChain& filter_chain)
      : called_(called), should_succeed_(should_succeed), server_config_(server_config),
        version_(version), chlo_hash_(chlo_hash), expected_filter_chain_(filter_chain) {
    ON_CALL(client_context_config_, cipherSuites)
        .WillByDefault(ReturnRef(
            Extensions::TransportSockets::Tls::ClientContextConfigImpl::DEFAULT_CIPHER_SUITES));
    ON_CALL(client_context_config_, ecdhCurves)
        .WillByDefault(
            ReturnRef(Extensions::TransportSockets::Tls::ClientContextConfigImpl::DEFAULT_CURVES));
    const std::string alpn("h2,http/1.1");
    ON_CALL(client_context_config_, alpnProtocols()).WillByDefault(ReturnRef(alpn));
    const std::string empty_string;
    ON_CALL(client_context_config_, serverNameIndication()).WillByDefault(ReturnRef(empty_string));
    ON_CALL(client_context_config_, signingAlgorithmsForTest())
        .WillByDefault(ReturnRef(empty_string));
    ON_CALL(client_context_config_, certificateValidationContext())
        .WillByDefault(Return(&cert_validation_ctx_config_));

    // Getting the last cert in the chain as the root CA cert.
    std::string cert_chain(quic::test::kTestCertificateChainPem);
    const std::string& root_ca_cert =
        cert_chain.substr(cert_chain.rfind("-----BEGIN CERTIFICATE-----"));
    const std::string path_string("some_path");
    ON_CALL(cert_validation_ctx_config_, caCert()).WillByDefault(ReturnRef(root_ca_cert));
    ON_CALL(cert_validation_ctx_config_, caCertPath()).WillByDefault(ReturnRef(path_string));
    ON_CALL(cert_validation_ctx_config_, trustChainVerification)
        .WillByDefault(Return(envoy::extensions::transport_sockets::tls::v3::
                                  CertificateValidationContext::VERIFY_TRUST_CHAIN));
    ON_CALL(cert_validation_ctx_config_, allowExpiredCertificate()).WillByDefault(Return(true));
    const std::string crl_list;
    ON_CALL(cert_validation_ctx_config_, certificateRevocationList())
        .WillByDefault(ReturnRef(crl_list));
    ON_CALL(cert_validation_ctx_config_, certificateRevocationListPath())
        .WillByDefault(ReturnRef(path_string));
    const std::vector<std::string> empty_string_list;
    ON_CALL(cert_validation_ctx_config_, verifySubjectAltNameList())
        .WillByDefault(ReturnRef(empty_string_list));
    const std::vector<envoy::type::matcher::v3::StringMatcher> san_matchers;
    ON_CALL(cert_validation_ctx_config_, subjectAltNameMatchers())
        .WillByDefault(ReturnRef(san_matchers));
    ON_CALL(cert_validation_ctx_config_, verifyCertificateHashList())
        .WillByDefault(ReturnRef(empty_string_list));
    ON_CALL(cert_validation_ctx_config_, verifyCertificateSpkiList())
        .WillByDefault(ReturnRef(empty_string_list));
    verifier_ =
        std::make_unique<EnvoyQuicProofVerifier>(store_, client_context_config_, time_system_);
  }

  // quic::ProofSource::Callback
  void Run(bool ok, const quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>& chain,
           const quic::QuicCryptoProof& proof,
           std::unique_ptr<quic::ProofSource::Details> details) override {
    called_ = true;
    if (!should_succeed_) {
      EXPECT_FALSE(ok);
      return;
    };
    EXPECT_TRUE(ok);
    EXPECT_EQ(2, chain->certs.size());
    std::string error;
    EXPECT_EQ(quic::QUIC_SUCCESS,
              verifier_->VerifyProof("www.example.org", 54321, server_config_, version_, chlo_hash_,
                                     chain->certs, proof.leaf_cert_scts, proof.signature, nullptr,
                                     &error, nullptr, nullptr))
        << error;
    EXPECT_EQ(&expected_filter_chain_,
              &static_cast<EnvoyQuicProofSourceDetails*>(details.get())->filterChain());
  }

private:
  bool& called_;
  bool should_succeed_;
  const std::string& server_config_;
  const quic::QuicTransportVersion& version_;
  quiche::QuicheStringPiece chlo_hash_;
  Network::FilterChain& expected_filter_chain_;
  NiceMock<Stats::MockStore> store_;
  Event::GlobalTimeSystem time_system_;
  NiceMock<Ssl::MockClientContextConfig> client_context_config_;
  NiceMock<Ssl::MockCertificateValidationContextConfig> cert_validation_ctx_config_;
  std::unique_ptr<EnvoyQuicProofVerifier> verifier_;
};

class TestSignatureCallback : public quic::ProofSource::SignatureCallback {
public:
  TestSignatureCallback(bool expect_success) : expect_success_(expect_success) {}
  ~TestSignatureCallback() override { EXPECT_TRUE(run_called_); }

  // quic::ProofSource::SignatureCallback
  void Run(bool ok, std::string, std::unique_ptr<quic::ProofSource::Details>) override {
    EXPECT_EQ(expect_success_, ok);
    run_called_ = true;
  }

private:
  bool expect_success_;
  bool run_called_{false};
};

class EnvoyQuicProofSourceTest : public ::testing::Test {
public:
  EnvoyQuicProofSourceTest()
      : server_address_(quic::QuicIpAddress::Loopback4(), 12345),
        client_address_(quic::QuicIpAddress::Loopback4(), 54321),
        listener_stats_({ALL_LISTENER_STATS(POOL_COUNTER(listener_config_.listenerScope()),
                                            POOL_GAUGE(listener_config_.listenerScope()),
                                            POOL_HISTOGRAM(listener_config_.listenerScope()))}),
        proof_source_(listen_socket_, filter_chain_manager_, listener_stats_) {}

protected:
  std::string hostname_{"www.fake.com"};
  quic::QuicSocketAddress server_address_;
  quic::QuicSocketAddress client_address_;
  quic::QuicTransportVersion version_{quic::QUIC_VERSION_UNSUPPORTED};
  quiche::QuicheStringPiece chlo_hash_{"aaaaa"};
  std::string server_config_{"Server Config"};
  std::string expected_certs_{quic::test::kTestCertificateChainPem};
  std::string pkey_{quic::test::kTestCertificatePrivateKeyPem};
  Network::MockFilterChain filter_chain_;
  Network::MockFilterChainManager filter_chain_manager_;
  Network::MockListenSocket listen_socket_;
  testing::NiceMock<Network::MockListenerConfig> listener_config_;
  Server::ListenerStats listener_stats_;
  EnvoyQuicProofSource proof_source_;
};

TEST_F(EnvoyQuicProofSourceTest, TestGetProof) {
  bool called = false;
  auto callback = std::make_unique<TestGetProofCallback>(called, true, server_config_, version_,
                                                         chlo_hash_, filter_chain_);
  EXPECT_CALL(listen_socket_, ioHandle()).Times(2);
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_))
      .WillRepeatedly(Invoke([&](const Network::ConnectionSocket& connection_socket) {
        EXPECT_EQ(*quicAddressToEnvoyAddressInstance(server_address_),
                  *connection_socket.localAddress());
        EXPECT_EQ(*quicAddressToEnvoyAddressInstance(client_address_),
                  *connection_socket.remoteAddress());
        EXPECT_EQ(Extensions::TransportSockets::TransportProtocolNames::get().Quic,
                  connection_socket.detectedTransportProtocol());
        EXPECT_EQ("h2", connection_socket.requestedApplicationProtocols()[0]);
        return &filter_chain_;
      }));
  auto server_context_config = std::make_unique<Ssl::MockServerContextConfig>();
  auto server_context_config_ptr = server_context_config.get();
  QuicServerTransportSocketFactory transport_socket_factory(std::move(server_context_config));
  EXPECT_CALL(filter_chain_, transportSocketFactory())
      .WillRepeatedly(ReturnRef(transport_socket_factory));

  Ssl::MockTlsCertificateConfig tls_cert_config;
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs{
      std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>(tls_cert_config)};
  EXPECT_CALL(*server_context_config_ptr, tlsCertificates())
      .WillRepeatedly(Return(tls_cert_configs));
  EXPECT_CALL(tls_cert_config, certificateChain()).WillOnce(ReturnRef(expected_certs_));
  EXPECT_CALL(tls_cert_config, privateKey()).WillOnce(ReturnRef(pkey_));
  proof_source_.GetProof(server_address_, client_address_, hostname_, server_config_, version_,
                         chlo_hash_, std::move(callback));
  EXPECT_TRUE(called);
}

TEST_F(EnvoyQuicProofSourceTest, GetProofFailNoFilterChain) {
  bool called = false;
  auto callback = std::make_unique<TestGetProofCallback>(called, false, server_config_, version_,
                                                         chlo_hash_, filter_chain_);
  EXPECT_CALL(listen_socket_, ioHandle());
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_))
      .WillRepeatedly(Invoke([&](const Network::ConnectionSocket&) { return nullptr; }));
  proof_source_.GetProof(server_address_, client_address_, hostname_, server_config_, version_,
                         chlo_hash_, std::move(callback));
  EXPECT_TRUE(called);
}

TEST_F(EnvoyQuicProofSourceTest, GetProofFailInvalidCert) {
  bool called = false;
  auto callback = std::make_unique<TestGetProofCallback>(called, false, server_config_, version_,
                                                         chlo_hash_, filter_chain_);
  EXPECT_CALL(listen_socket_, ioHandle());
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_))
      .WillRepeatedly(Invoke([&](const Network::ConnectionSocket&) { return &filter_chain_; }));
  auto server_context_config = std::make_unique<Ssl::MockServerContextConfig>();
  auto server_context_config_ptr = server_context_config.get();
  QuicServerTransportSocketFactory transport_socket_factory(std::move(server_context_config));
  EXPECT_CALL(filter_chain_, transportSocketFactory())
      .WillRepeatedly(ReturnRef(transport_socket_factory));

  Ssl::MockTlsCertificateConfig tls_cert_config;
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs{
      std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>(tls_cert_config)};
  EXPECT_CALL(*server_context_config_ptr, tlsCertificates())
      .WillRepeatedly(Return(tls_cert_configs));
  std::string invalid_cert{"invalid certificate"};
  EXPECT_CALL(tls_cert_config, certificateChain()).WillOnce(ReturnRef(invalid_cert));
  std::string path("some path");
  EXPECT_CALL(tls_cert_config, certificateChainPath()).WillOnce(ReturnRef(path));
  proof_source_.GetProof(server_address_, client_address_, hostname_, server_config_, version_,
                         chlo_hash_, std::move(callback));
  EXPECT_TRUE(called);
}

TEST_F(EnvoyQuicProofSourceTest, InvalidPrivateKey) {
  EXPECT_CALL(listen_socket_, ioHandle());
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_))
      .WillOnce(Invoke([&](const Network::ConnectionSocket&) { return &filter_chain_; }));
  auto server_context_config = std::make_unique<Ssl::MockServerContextConfig>();
  auto server_context_config_ptr = server_context_config.get();
  QuicServerTransportSocketFactory transport_socket_factory(std::move(server_context_config));
  EXPECT_CALL(filter_chain_, transportSocketFactory())
      .WillRepeatedly(ReturnRef(transport_socket_factory));

  Ssl::MockTlsCertificateConfig tls_cert_config;
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs{
      std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>(tls_cert_config)};
  EXPECT_CALL(*server_context_config_ptr, tlsCertificates())
      .WillRepeatedly(Return(tls_cert_configs));
  std::string invalid_pkey("abcdefg");
  EXPECT_CALL(tls_cert_config, privateKey()).WillOnce(ReturnRef(invalid_pkey));
  proof_source_.ComputeTlsSignature(server_address_, client_address_, hostname_,
                                    SSL_SIGN_RSA_PSS_RSAE_SHA256, "payload",
                                    std::make_unique<TestSignatureCallback>(false));
}

} // namespace Quic
} // namespace Envoy
