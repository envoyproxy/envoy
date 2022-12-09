#include <memory>
#include <string>
#include <vector>

#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"

#include "test/common/quic/test_utils.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/test_tools/test_certificates.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {

namespace Quic {

class SignatureVerifier {
public:
  SignatureVerifier() {
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
    const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>
        san_matchers;
    ON_CALL(cert_validation_ctx_config_, subjectAltNameMatchers())
        .WillByDefault(ReturnRef(san_matchers));
    ON_CALL(cert_validation_ctx_config_, verifyCertificateHashList())
        .WillByDefault(ReturnRef(empty_string_list));
    ON_CALL(cert_validation_ctx_config_, verifyCertificateSpkiList())
        .WillByDefault(ReturnRef(empty_string_list));
    const absl::optional<envoy::config::core::v3::TypedExtensionConfig> nullopt = absl::nullopt;
    ON_CALL(cert_validation_ctx_config_, customValidatorConfig()).WillByDefault(ReturnRef(nullopt));
    auto context = std::make_shared<Extensions::TransportSockets::Tls::ClientContextImpl>(
        store_, client_context_config_, time_system_);
    ON_CALL(verify_context_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(verify_context_, transportSocketOptions())
        .WillByDefault(ReturnRef(transport_socket_options_));
    verifier_ = std::make_unique<EnvoyQuicProofVerifier>(std::move(context));
  }

  void verifyCertsAndSignature(
      const quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain>& chain,
      const std::string& payload, const std::string& signature) {
    const std::string& leaf = chain->certs[0];
    std::unique_ptr<quic::CertificateView> cert_view =
        quic::CertificateView::ParseSingleCertificate(leaf);
    ASSERT_NE(cert_view, nullptr);
    std::string error_details;
    int sign_alg = deduceSignatureAlgorithmFromPublicKey(cert_view->public_key(), &error_details);
    EXPECT_NE(sign_alg, 0);
    EXPECT_TRUE(cert_view->VerifySignature(payload, signature, sign_alg));

    std::string error;
    std::unique_ptr<quic::ProofVerifyDetails> verify_details;
    EXPECT_EQ(quic::QUIC_SUCCESS,
              verifier_->VerifyCertChain("www.example.org", 54321, chain->certs,
                                         /*ocsp_response=*/"", /*cert_sct=*/"Fake SCT",
                                         &verify_context_, &error, &verify_details,
                                         /*out_alert=*/nullptr,
                                         /*callback=*/nullptr))
        << error;
  }

private:
  NiceMock<Stats::MockStore> store_;
  Event::GlobalTimeSystem time_system_;
  NiceMock<Ssl::MockClientContextConfig> client_context_config_;
  NiceMock<Ssl::MockCertificateValidationContextConfig> cert_validation_ctx_config_;
  std::unique_ptr<EnvoyQuicProofVerifier> verifier_;
  NiceMock<Ssl::MockContextManager> tls_context_manager_;
  Event::MockDispatcher dispatcher_;
  NiceMock<MockProofVerifyContext> verify_context_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
};

class TestSignatureCallback : public quic::ProofSource::SignatureCallback {
public:
  TestSignatureCallback(bool expect_success, Network::FilterChain& filter_chain,
                        std::string& signature)
      : expect_success_(expect_success), signature_(signature),
        expected_filter_chain_(filter_chain) {}
  ~TestSignatureCallback() override { EXPECT_TRUE(called_); }

  // quic::ProofSource::SignatureCallback
  void Run(bool ok, std::string signature,
           std::unique_ptr<quic::ProofSource::Details> details) override {
    called_ = true;
    EXPECT_EQ(expect_success_, ok);
    if (ok) {
      signature_ = signature;
      EXPECT_EQ(&expected_filter_chain_,
                &static_cast<EnvoyQuicProofSourceDetails*>(details.get())->filterChain());
    }
  }

private:
  bool expect_success_;
  bool called_;
  std::string& signature_;
  Network::FilterChain& expected_filter_chain_;
};

class EnvoyQuicProofSourceTest : public ::testing::Test {
public:
  EnvoyQuicProofSourceTest()
      : server_address_(quic::QuicIpAddress::Loopback4(), 12345),
        client_address_(quic::QuicIpAddress::Loopback4(), 54321),
        mock_context_config_(new Ssl::MockServerContextConfig()),
        listener_stats_({ALL_LISTENER_STATS(POOL_COUNTER(listener_config_.listenerScope()),
                                            POOL_GAUGE(listener_config_.listenerScope()),
                                            POOL_HISTOGRAM(listener_config_.listenerScope()))}),
        proof_source_(listen_socket_, filter_chain_manager_, listener_stats_, time_system_) {
    EXPECT_CALL(*mock_context_config_, setSecretUpdateCallback(_)).Times(testing::AtLeast(1u));
    transport_socket_factory_ = std::make_unique<QuicServerTransportSocketFactory>(
        true, listener_config_.listenerScope(),
        std::unique_ptr<Ssl::MockServerContextConfig>(mock_context_config_));
    transport_socket_factory_->initialize();
    EXPECT_CALL(filter_chain_, name()).WillRepeatedly(Return(""));
  }

  void expectCertChainAndPrivateKey(const std::string& cert, bool expect_private_key) {
    EXPECT_CALL(listen_socket_, ioHandle()).Times(expect_private_key ? 2u : 1u);
    EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _))
        .WillRepeatedly(Invoke(
            [&](const Network::ConnectionSocket& connection_socket, const StreamInfo::StreamInfo&) {
              EXPECT_EQ(*quicAddressToEnvoyAddressInstance(server_address_),
                        *connection_socket.connectionInfoProvider().localAddress());
              EXPECT_EQ(*quicAddressToEnvoyAddressInstance(client_address_),
                        *connection_socket.connectionInfoProvider().remoteAddress());
              EXPECT_EQ("quic", connection_socket.detectedTransportProtocol());
              EXPECT_EQ("h3", connection_socket.requestedApplicationProtocols()[0]);
              return &filter_chain_;
            }));
    EXPECT_CALL(filter_chain_, transportSocketFactory())
        .WillRepeatedly(ReturnRef(*transport_socket_factory_));

    EXPECT_CALL(*mock_context_config_, isReady()).WillRepeatedly(Return(true));
    std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs{
        std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>(tls_cert_config_)};
    EXPECT_CALL(*mock_context_config_, tlsCertificates()).WillRepeatedly(Return(tls_cert_configs));
    EXPECT_CALL(tls_cert_config_, certificateChain()).WillOnce(ReturnRef(cert));
    if (expect_private_key) {
      EXPECT_CALL(tls_cert_config_, privateKey()).WillOnce(ReturnRef(pkey_));
    }
  }

protected:
  std::string hostname_{"www.fake.com"};
  quic::QuicSocketAddress server_address_;
  quic::QuicSocketAddress client_address_;
  quic::QuicTransportVersion version_{quic::QUIC_VERSION_UNSUPPORTED};
  absl::string_view chlo_hash_{"aaaaa"};
  std::string server_config_{"Server Config"};
  std::string expected_certs_{quic::test::kTestCertificateChainPem};
  std::string pkey_{quic::test::kTestCertificatePrivateKeyPem};
  Network::MockFilterChain filter_chain_;
  Network::MockFilterChainManager filter_chain_manager_;
  Network::MockListenSocket listen_socket_;
  testing::NiceMock<Network::MockListenerConfig> listener_config_;
  Ssl::MockServerContextConfig* mock_context_config_;
  std::unique_ptr<QuicServerTransportSocketFactory> transport_socket_factory_;
  Ssl::MockTlsCertificateConfig tls_cert_config_;
  Server::ListenerStats listener_stats_;
  Event::GlobalTimeSystem time_system_;
  EnvoyQuicProofSource proof_source_;
};

TEST_F(EnvoyQuicProofSourceTest, TestGetCerChainAndSignatureAndVerify) {
  expectCertChainAndPrivateKey(expected_certs_, true);
  bool cert_matched_sni;
  quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain> chain =
      proof_source_.GetCertChain(server_address_, client_address_, hostname_, &cert_matched_sni);
  EXPECT_EQ(2, chain->certs.size());

  std::string error_details;
  bssl::UniquePtr<X509> cert = parseDERCertificate(chain->certs[0], &error_details);
  EXPECT_NE(cert, nullptr);
  bssl::UniquePtr<EVP_PKEY> pub_key(X509_get_pubkey(cert.get()));
  int sign_alg = deduceSignatureAlgorithmFromPublicKey(pub_key.get(), &error_details);
  EXPECT_EQ(sign_alg, SSL_SIGN_RSA_PSS_RSAE_SHA256);
  std::string signature;
  proof_source_.ComputeTlsSignature(
      server_address_, client_address_, hostname_, SSL_SIGN_RSA_PSS_RSAE_SHA256, "payload",
      std::make_unique<TestSignatureCallback>(true, filter_chain_, signature));
  SignatureVerifier verifier;
  verifier.verifyCertsAndSignature(chain, "payload", signature);
}

TEST_F(EnvoyQuicProofSourceTest, GetCertChainFailBadConfig) {
  // No filter chain.
  EXPECT_CALL(listen_socket_, ioHandle()).Times(3);
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _))
      .WillOnce(Invoke([&](const Network::ConnectionSocket&, const StreamInfo::StreamInfo&) {
        return nullptr;
      }));
  bool cert_matched_sni;
  EXPECT_EQ(nullptr, proof_source_.GetCertChain(server_address_, client_address_, hostname_,
                                                &cert_matched_sni));

  // Cert not ready.
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _))
      .WillOnce(Invoke([&](const Network::ConnectionSocket&, const StreamInfo::StreamInfo&) {
        return &filter_chain_;
      }));
  EXPECT_CALL(filter_chain_, transportSocketFactory())
      .WillOnce(ReturnRef(*transport_socket_factory_));
  EXPECT_CALL(*mock_context_config_, isReady()).WillOnce(Return(false));
  EXPECT_EQ(nullptr, proof_source_.GetCertChain(server_address_, client_address_, hostname_,
                                                &cert_matched_sni));

  // No certs in config.
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _))
      .WillRepeatedly(Invoke(
          [&](const Network::ConnectionSocket& connection_socket, const StreamInfo::StreamInfo&) {
            EXPECT_EQ(*quicAddressToEnvoyAddressInstance(server_address_),
                      *connection_socket.connectionInfoProvider().localAddress());
            EXPECT_EQ(*quicAddressToEnvoyAddressInstance(client_address_),
                      *connection_socket.connectionInfoProvider().remoteAddress());
            EXPECT_EQ("quic", connection_socket.detectedTransportProtocol());
            EXPECT_EQ("h3", connection_socket.requestedApplicationProtocols()[0]);
            return &filter_chain_;
          }));
  EXPECT_CALL(filter_chain_, transportSocketFactory())
      .WillOnce(ReturnRef(*transport_socket_factory_));
  EXPECT_CALL(*mock_context_config_, isReady()).WillOnce(Return(true));
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs{};
  EXPECT_CALL(*mock_context_config_, tlsCertificates()).WillOnce(Return(tls_cert_configs));
  EXPECT_EQ(nullptr, proof_source_.GetCertChain(server_address_, client_address_, hostname_,
                                                &cert_matched_sni));
}

TEST_F(EnvoyQuicProofSourceTest, GetCertChainFailInvalidCert) {
  std::string invalid_cert{R"(-----BEGIN CERTIFICATE-----
    invalid certificate
    -----END CERTIFICATE-----)"};
  expectCertChainAndPrivateKey(invalid_cert, false);
  bool cert_matched_sni;
  EXPECT_EQ(nullptr, proof_source_.GetCertChain(server_address_, client_address_, hostname_,
                                                &cert_matched_sni));
}

TEST_F(EnvoyQuicProofSourceTest, GetCertChainFailInvalidPublicKeyInCert) {
  // This is a valid cert with RSA public key. But we don't support RSA key with
  // length < 1024.
  std::string cert_with_rsa_1024{R"(-----BEGIN CERTIFICATE-----
MIIC2jCCAkOgAwIBAgIUDBHEwlCvLGh3w0O8VwIW+CjYXY8wDQYJKoZIhvcNAQEL
BQAwfzELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAk1BMRIwEAYDVQQHDAlDYW1icmlk
Z2UxDzANBgNVBAoMBkdvb2dsZTEOMAwGA1UECwwFZW52b3kxDTALBgNVBAMMBHRl
c3QxHzAdBgkqhkiG9w0BCQEWEGRhbnpoQGdvb2dsZS5jb20wHhcNMjAwODA0MTg1
OTQ4WhcNMjEwODA0MTg1OTQ4WjB/MQswCQYDVQQGEwJVUzELMAkGA1UECAwCTUEx
EjAQBgNVBAcMCUNhbWJyaWRnZTEPMA0GA1UECgwGR29vZ2xlMQ4wDAYDVQQLDAVl
bnZveTENMAsGA1UEAwwEdGVzdDEfMB0GCSqGSIb3DQEJARYQZGFuemhAZ29vZ2xl
LmNvbTCBnzANBgkqhkiG9w0BAQEFAAOBjQAwgYkCgYEAykCZNjxws+sNfnp18nsp
+7LN81J/RSwAHLkGnwEtd3OxSUuiCYHgYlyuEAwJdf99+SaFrgcA4LvYJ/Mhm/fZ
msnpfsAvoQ49+ax0fm1x56ii4KgNiu9iFsWwwVmkHkgjlRcRsmhr4WeIf14Yvpqs
JNsbNVSCZ4GLQ2V6BqIHlhcCAwEAAaNTMFEwHQYDVR0OBBYEFDO1KPYcdRmeKDvL
H2Yzj8el2Xe1MB8GA1UdIwQYMBaAFDO1KPYcdRmeKDvLH2Yzj8el2Xe1MA8GA1Ud
EwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADgYEAnwWVmwSK9TDml7oHGBavzOC1
f/lOd5zz2e7Tu2pUtx1sX1tlKph1D0ANpJwxRV78R2hjmynLSl7h4Ual9NMubqkD
x96rVeUbRJ/qU4//nNM/XQa9vIAIcTZ0jFhmb0c3R4rmoqqC3vkSDwtaE5yuS5T4
GUy+n0vQNB0cXGzgcGI=
-----END CERTIFICATE-----)"};
  expectCertChainAndPrivateKey(cert_with_rsa_1024, false);
  bool cert_matched_sni;
  EXPECT_EQ(nullptr, proof_source_.GetCertChain(server_address_, client_address_, hostname_,
                                                &cert_matched_sni));
}

TEST_F(EnvoyQuicProofSourceTest, ComputeSignatureFailNoFilterChain) {
  EXPECT_CALL(listen_socket_, ioHandle());
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _))
      .WillOnce(Invoke([&](const Network::ConnectionSocket&, const StreamInfo::StreamInfo&) {
        return nullptr;
      }));

  std::string signature;
  proof_source_.ComputeTlsSignature(
      server_address_, client_address_, hostname_, SSL_SIGN_RSA_PSS_RSAE_SHA256, "payload",
      std::make_unique<TestSignatureCallback>(false, filter_chain_, signature));
}

TEST_F(EnvoyQuicProofSourceTest, UnexpectedPrivateKey) {
  EXPECT_CALL(listen_socket_, ioHandle());
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _))
      .WillOnce(Invoke([&](const Network::ConnectionSocket&, const StreamInfo::StreamInfo&) {
        return &filter_chain_;
      }));
  EXPECT_CALL(filter_chain_, transportSocketFactory())
      .WillRepeatedly(ReturnRef(*transport_socket_factory_));

  Ssl::MockTlsCertificateConfig tls_cert_config;
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs{
      std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>(tls_cert_config)};
  EXPECT_CALL(*mock_context_config_, tlsCertificates()).WillRepeatedly(Return(tls_cert_configs));
  EXPECT_CALL(*mock_context_config_, isReady()).WillOnce(Return(true));
  std::string rsa_pkey_1024_len(R"(-----BEGIN RSA PRIVATE KEY-----
MIICWwIBAAKBgQC79hDq/OwN3ke3EF6Ntdi9R+VSrl9MStk992l1us8lZhq+e0zU
OlvxbUeZ8wyVkzs1gqI1it1IwF+EpdGhHhjggZjg040GD3HWSuyCzpHh+nLwJxtQ
D837PCg0zl+TnKv1YjY3I1F3trGhIqfd2B6pgaJ4hpr+0hdqnKP0Htd4DwIDAQAB
AoGASNypUD59Tx70k+1fifWNMEq3heacgJmfPxsyoXWqKSg8g8yOStLYo20mTXJf
VXg+go7CTJkpELOqE2SoL5nYMD0D/YIZCgDx85k0GWHdA6udNn4to95ZTeZPrBHx
T0QNQHnZI3A7RwLinO60IRY0NYzhkTEBxIuvIY6u0DVbrAECQQDpshbxK3DHc7Yi
Au7BUsxP8RbG4pP5IIVoD4YvJuwUkdrfrwejqTdkfchJJc+Gu/+h8vy7eASPHLLT
NBk5wFoPAkEAzeaKnx0CgNs0RX4+sSF727FroD98VUM38OFEJQ6U9OAWGvaKd8ey
yAYUjR2Sl5ZRyrwWv4IqyWgUGhZqNG0CAQJAPTjjm8DGpenhcB2WkNzxG4xMbEQV
gfGMIYvXmmi29liTn4AKH00IbvIo00jtih2cRcATh8VUZG2fR4dhiGik7wJAWSwS
NwzaS7IjtkERp6cHvELfiLxV/Zsp/BGjcKUbD96I1E6X834ySHyRo/f9x9bbP4Es
HO6j1yxTIGU6w8++AQJACdFPnRidOaj5oJmcZq0s6WGTYfegjTOKgi5KQzO0FTwG
qGm130brdD+1U1EJnEFmleLZ/W6mEi3MxcKpWOpTqQ==
-----END RSA PRIVATE KEY-----)");
  EXPECT_CALL(tls_cert_config, privateKey()).WillOnce(ReturnRef(rsa_pkey_1024_len));
  std::string signature;
  proof_source_.ComputeTlsSignature(
      server_address_, client_address_, hostname_, SSL_SIGN_RSA_PSS_RSAE_SHA256, "payload",
      std::make_unique<TestSignatureCallback>(false, filter_chain_, signature));
}

TEST_F(EnvoyQuicProofSourceTest, InvalidPrivateKey) {
  EXPECT_CALL(listen_socket_, ioHandle());
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _))
      .WillOnce(Invoke([&](const Network::ConnectionSocket&, const StreamInfo::StreamInfo&) {
        return &filter_chain_;
      }));
  EXPECT_CALL(filter_chain_, transportSocketFactory())
      .WillRepeatedly(ReturnRef(*transport_socket_factory_));

  Ssl::MockTlsCertificateConfig tls_cert_config;
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs{
      std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>(tls_cert_config)};
  EXPECT_CALL(*mock_context_config_, tlsCertificates()).WillRepeatedly(Return(tls_cert_configs));
  EXPECT_CALL(*mock_context_config_, isReady()).WillOnce(Return(true));
  std::string invalid_pkey("abcdefg");
  EXPECT_CALL(tls_cert_config, privateKey()).WillOnce(ReturnRef(invalid_pkey));
  std::string signature;
  proof_source_.ComputeTlsSignature(
      server_address_, client_address_, hostname_, SSL_SIGN_RSA_PSS_RSAE_SHA256, "payload",
      std::make_unique<TestSignatureCallback>(false, filter_chain_, signature));
}

} // namespace Quic
} // namespace Envoy
