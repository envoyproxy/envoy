#include <memory>
#include <string>
#include <vector>

#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_verifier.h"
#include "extensions/quic_listeners/quiche/envoy_quic_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

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
  TestGetProofCallback(bool& called, std::string leaf_cert_scts, const absl::string_view cert,
                       Network::FilterChain& filter_chain)
      : called_(called), expected_leaf_certs_scts_(std::move(leaf_cert_scts)),
        expected_leaf_cert_(cert), expected_filter_chain_(filter_chain) {}

  // quic::ProofSource::Callback
  void Run(bool ok, const quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>& chain,
           const quic::QuicCryptoProof& proof,
           std::unique_ptr<quic::ProofSource::Details> details) override {
    EXPECT_TRUE(ok);
    EXPECT_EQ(expected_leaf_certs_scts_, proof.leaf_cert_scts);
    EXPECT_EQ(2, chain->certs.size());
    EXPECT_EQ(expected_leaf_cert_, chain->certs[0]);
    EXPECT_EQ(&expected_filter_chain_,
              &static_cast<EnvoyQuicProofSourceDetails*>(details.get())->filterChain());
    called_ = true;
  }

private:
  bool& called_;
  std::string expected_leaf_certs_scts_;
  absl::string_view expected_leaf_cert_;
  Network::FilterChain& expected_filter_chain_;
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
  quiche::QuicheStringPiece chlo_hash_{""};
  std::string server_config_{"Server Config"};
  std::string expected_certs_{quic::test::kTestCertificateChainPem};
  std::string pkey_{quic::test::kTestCertificatePrivateKeyPem};
  Network::MockFilterChain filter_chain_;
  Network::MockFilterChainManager filter_chain_manager_;
  Network::MockListenSocket listen_socket_;
  testing::NiceMock<Network::MockListenerConfig> listener_config_;
  Server::ListenerStats listener_stats_;
  EnvoyQuicProofSource proof_source_;
  EnvoyQuicFakeProofVerifier proof_verifier_;
};

TEST_F(EnvoyQuicProofSourceTest, TestGetProof) {
  bool called = false;
  auto callback = std::make_unique<TestGetProofCallback>(
      called, "Fake timestamp", quic::test::kTestCertificate, filter_chain_);
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

  EXPECT_EQ(quic::QUIC_SUCCESS,
            proof_verifier_.VerifyProof(hostname_, /*port=*/0, server_config_, version_, chlo_hash_,
                                        {"Fake cert"}, "", "fake signature", nullptr, nullptr,
                                        nullptr, nullptr));
  EXPECT_EQ(quic::QUIC_FAILURE,
            proof_verifier_.VerifyProof(hostname_, /*port=*/0, server_config_, version_, chlo_hash_,
                                        {"Fake cert", "Unexpected cert"}, "Fake timestamp",
                                        "fake signature", nullptr, nullptr, nullptr, nullptr));
}

} // namespace Quic
} // namespace Envoy
