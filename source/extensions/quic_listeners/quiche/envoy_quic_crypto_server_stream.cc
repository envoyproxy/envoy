#include "extensions/quic_listeners/quiche/envoy_quic_crypto_server_stream.h"

namespace Envoy {
namespace Quic {

void EnvoyQuicCryptoServerStream::EnvoyProcessClientHelloResultCallback::Run(
    quic::QuicErrorCode error, const std::string& error_details,
    std::unique_ptr<quic::CryptoHandshakeMessage> message,
    std::unique_ptr<quic::DiversificationNonce> diversification_nonce,
    std::unique_ptr<quic::ProofSource::Details> proof_source_details) {
  if (parent_ == nullptr) {
    return;
  }

  if (proof_source_details != nullptr) {
    // Retain a copy of the proof source details after getting filter chain.
    parent_->details_ = std::make_unique<EnvoyQuicProofSourceDetails>(
        static_cast<EnvoyQuicProofSourceDetails&>(*proof_source_details));
  }
  parent_->done_cb_wrapper_ = nullptr;
  parent_ = nullptr;
  done_cb_->Run(error, error_details, std::move(message), std::move(diversification_nonce),
                std::move(proof_source_details));
}

EnvoyQuicCryptoServerStream::~EnvoyQuicCryptoServerStream() {
  if (done_cb_wrapper_ != nullptr) {
    done_cb_wrapper_->cancel();
  }
}

void EnvoyQuicCryptoServerStream::ProcessClientHello(
    quic::QuicReferenceCountedPointer<quic::ValidateClientHelloResultCallback::Result> result,
    std::unique_ptr<quic::ProofSource::Details> proof_source_details,
    std::unique_ptr<quic::ProcessClientHelloResultCallback> done_cb) {
  auto done_cb_wrapper =
      std::make_unique<EnvoyProcessClientHelloResultCallback>(this, std::move(done_cb));
  ASSERT(done_cb_wrapper_ == nullptr);
  done_cb_wrapper_ = done_cb_wrapper.get();
  // Old QUICHE code might call GetProof() earlier and pass in proof source instance here. But this
  // is no longer the case, so proof_source_details should always be null.
  ASSERT(proof_source_details == nullptr);
  quic::QuicCryptoServerStream::ProcessClientHello(result, std::move(proof_source_details),
                                                   std::move(done_cb_wrapper));
}

} // namespace Quic
} // namespace Envoy
