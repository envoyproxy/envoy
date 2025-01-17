#pragma once

#include <memory>

#include "source/common/quic/envoy_quic_proof_source_base.h"

#include "test/mocks/network/mocks.h"

#include "quiche/quic/test_tools/test_certificates.h"

namespace Envoy {
namespace Quic {

// A test ProofSource which always provide a hard-coded test certificate in
// QUICHE and a fake signature.
class TestProofSource : public EnvoyQuicProofSourceBase {
public:
  quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain>
  GetCertChain(const quic::QuicSocketAddress& /*server_address*/,
               const quic::QuicSocketAddress& /*client_address*/, const std::string& /*hostname*/,
               bool* cert_matched_sni) override {
    *cert_matched_sni = true;
    return cert_chain_;
  }

  const Network::MockFilterChain& filterChain() const { return filter_chain_; }

protected:
  void signPayload(const quic::QuicSocketAddress& /*server_address*/,
                   const quic::QuicSocketAddress& /*client_address*/,
                   const std::string& /*hostname*/, uint16_t /*signature_algorithm*/,
                   absl::string_view in,
                   std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override {
    callback->Run(true, absl::StrCat("Fake signature for { ", in, " }"),
                  std::make_unique<EnvoyQuicProofSourceDetails>(filter_chain_));
  }

private:
  quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain> cert_chain_{
      new quic::ProofSource::Chain(
          std::vector<std::string>{std::string(quic::test::kTestCertificate)})};

  Network::MockFilterChain filter_chain_;
};

} // namespace Quic
} // namespace Envoy
