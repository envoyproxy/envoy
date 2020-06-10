#ifdef __GNUC__
#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"
#include "quiche/quic/test_tools/test_certificates.h"

#pragma GCC diagnostic pop
#else
#include "quiche/quic/test_tools/test_certificates.h"
#endif

#include <memory>
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_source.h"

namespace Envoy {
namespace Quic {

// A test ProofSource which always provide a hard-coded test certificate in
// QUICHE and a fake signature.
class TestProofSource : public Quic::EnvoyQuicFakeProofSource {
public:
  quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>
  GetCertChain(const quic::QuicSocketAddress& /*server_address*/,
               const quic::QuicSocketAddress& /*client_address*/,
               const std::string& /*hostname*/) override {
    return cert_chain_;
  }

  void
  ComputeTlsSignature(const quic::QuicSocketAddress& /*server_address*/,
                      const quic::QuicSocketAddress& /*client_address*/,
                      const std::string& /*hostname*/, uint16_t /*signature_algorithm*/,
                      quiche::QuicheStringPiece in,
                      std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override {
    callback->Run(true, absl::StrCat("Fake signature for { ", in, " }"), nullptr);
  }

private:
  quic::QuicReferenceCountedPointer<quic::ProofSource::Chain> cert_chain_{
      new quic::ProofSource::Chain(
          std::vector<std::string>{std::string(quic::test::kTestCertificate)})};
};

} // namespace Quic
} // namespace Envoy
