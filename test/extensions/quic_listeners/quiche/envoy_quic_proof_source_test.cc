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
  TestGetProofCallback(bool& called, std::string signature, std::string leaf_cert_scts,
                       std::vector<std::string> certs)
      : called_(called), expected_signature_(std::move(signature)),
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
    called_ = true;
  }

private:
  bool& called_;
  std::string expected_signature_;
  std::string expected_leaf_certs_scts_;
  std::vector<std::string> expected_certs_;
};

class EnvoyQuicFakeProofSourceTest : public ::testing::Test {
protected:
  std::string hostname_{"www.fake.com"};
  quic::QuicSocketAddress server_address_;
  quic::QuicTransportVersion version_{quic::QUIC_VERSION_UNSUPPORTED};
  quiche::QuicheStringPiece chlo_hash_{""};
  std::string server_config_{"Server Config"};
  std::vector<std::string> expected_certs_{"Fake cert"};
  std::string expected_signature_{absl::StrCat("Fake signature for { ", server_config_, " }")};
  EnvoyQuicFakeProofSource proof_source_;
  EnvoyQuicFakeProofVerifier proof_verifier_;
};

TEST_F(EnvoyQuicFakeProofSourceTest, TestGetProof) {
  bool called = false;
  auto callback = std::make_unique<TestGetProofCallback>(called, expected_signature_,
                                                         "Fake timestamp", expected_certs_);
  proof_source_.GetProof(server_address_, hostname_, server_config_, version_, chlo_hash_,
                         std::move(callback));
  EXPECT_TRUE(called);
}

TEST_F(EnvoyQuicFakeProofSourceTest, TestVerifyProof) {
  EXPECT_EQ(quic::QUIC_SUCCESS,
            proof_verifier_.VerifyProof(hostname_, /*port=*/0, server_config_, version_, chlo_hash_,
                                        expected_certs_, "", expected_signature_, nullptr, nullptr,
                                        nullptr, nullptr));
  std::vector<std::string> wrong_certs{"wrong cert"};
  EXPECT_EQ(quic::QUIC_FAILURE,
            proof_verifier_.VerifyProof(hostname_, /*port=*/0, server_config_, version_, chlo_hash_,
                                        wrong_certs, "Fake timestamp", expected_signature_, nullptr,
                                        nullptr, nullptr, nullptr));
}

} // namespace Quic
} // namespace Envoy
