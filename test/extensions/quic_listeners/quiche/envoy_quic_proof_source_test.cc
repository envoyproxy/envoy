#include <string>
#include <vector>

#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_verifier.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

namespace Quic {

class TestGetProofCallback : public quic::ProofSource::Callback {
public:
  TestGetProofCallback(std::string signature, std::string leaf_cert_scts,
                       std::vector<std::string> certs)
      : expected_signature_(std::move(signature)),
        expected_leaf_certs_scts_(std::move(leaf_cert_scts)), expected_certs_(std::move(certs)) {}

  // quic::ProofSource::Callback
  void Run(bool ok, const quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>& chain,
           const quic::QuicCryptoProof& proof,
           std::unique_ptr<quic::ProofSource::Details> details) override {
    EXPECT_TRUE(ok);
    EXPECT_EQ(expected_signature_, proof.signature);
    EXPECT_EQ(expected_leaf_certs_scts_, proof.leaf_cert_scts);
    EXPECT_EQ(expected_certs_, chain->certs);
    EXPECT_EQ(nullptr, details);
  }

private:
  std::string expected_signature_;
  std::string expected_leaf_certs_scts_;
  std::vector<std::string> expected_certs_;
};

class EnvoyQuicProofSourceTest : public ::testing::Test {
protected:
  std::string hostname_{"www.dummy.com"};
  quic::QuicSocketAddress server_address_;
  quic::QuicTransportVersion version_{quic::QUIC_VERSION_UNSUPPORTED};
  quic::QuicStringPiece chlo_hash_{""};
  std::string server_config_{"Server Config"};
  std::vector<std::string> expected_certs_{absl::StrCat("Dummy cert from ", hostname_)};
  std::string expected_signature_{absl::StrCat("Dummy signature for { ", server_config_, " }")};
  EnvoyQuicFakeProofSource proof_source_;
  EnvoyQuicFakeProofVerifier proof_verifier_;
};

TEST_F(EnvoyQuicProofSourceTest, TestGetProof) {
  auto callback = std::make_unique<TestGetProofCallback>(expected_signature_, "Dummy timestamp",
                                                         expected_certs_);
  proof_source_.GetProof(server_address_, hostname_, server_config_, version_, chlo_hash_,
                         std::move(callback));
}

TEST_F(EnvoyQuicProofSourceTest, TestVerifyProof) {
  EXPECT_EQ(quic::QUIC_SUCCESS,
            proof_verifier_.VerifyProof(hostname_, /*port=*/0, server_config_, version_, chlo_hash_,
                                        expected_certs_, "Dummy timestamp", expected_signature_,
                                        nullptr, nullptr, nullptr, nullptr));
  std::vector<std::string> wrong_certs{"wrong cert"};
  EXPECT_EQ(quic::QUIC_FAILURE,
            proof_verifier_.VerifyProof(hostname_, /*port=*/0, server_config_, version_, chlo_hash_,
                                        wrong_certs, "Dummy timestamp", expected_signature_,
                                        nullptr, nullptr, nullptr, nullptr));
}

} // namespace Quic
} // namespace Envoy
