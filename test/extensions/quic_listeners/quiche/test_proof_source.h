#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

#include "quiche/quic/test_tools/test_certificates.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <memory>

#include "test/mocks/network/mocks.h"
#include "extensions/quic_listeners/quiche/envoy_quic_proof_source_base.h"

namespace Envoy {
namespace Quic {

// A test ProofSource which always provide a hard-coded test certificate in
// QUICHE and a fake signature.
class TestProofSource : public EnvoyQuicProofSourceBase {
public:
  quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>
  GetCertChain(const quic::QuicSocketAddress& /*server_address*/,
               const quic::QuicSocketAddress& /*client_address*/,
               const std::string& /*hostname*/) override {
    return cert_chain_;
  }

  const Network::MockFilterChain& filterChain() const { return filter_chain_; }

protected:
  void signPayload(const quic::QuicSocketAddress& /*server_address*/,
                   const quic::QuicSocketAddress& /*client_address*/,
                   const std::string& /*hostname*/, uint16_t /*signature_algorithm*/,
                   quiche::QuicheStringPiece in,
                   std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override {
    callback->Run(true, absl::StrCat("Fake signature for { ", in, " }"),
                  std::make_unique<EnvoyQuicProofSourceDetails>(filter_chain_));
  }

private:
  quic::QuicReferenceCountedPointer<quic::ProofSource::Chain> cert_chain_{
      new quic::ProofSource::Chain(
          std::vector<std::string>{std::string(quic::test::kTestCertificate)})};

  Network::MockFilterChain filter_chain_;
};

} // namespace Quic
} // namespace Envoy
