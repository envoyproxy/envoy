#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_crypto_server_stream.h"
#include "quiche/quic/core/tls_server_handshaker.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_proof_source.h"

#include <memory>

namespace Envoy {
namespace Quic {

class EnvoyCryptoServerStream : protected Logger::Loggable<Logger::Id::quic_stream> {
public:
  virtual ~EnvoyCryptoServerStream() = default;
  virtual const EnvoyQuicProofSourceDetails* proofSourceDetails() const = 0;
};

// A dedicated stream to do QUIC crypto handshake.
class EnvoyQuicCryptoServerStream : public quic::QuicCryptoServerStream,
                                    public EnvoyCryptoServerStream {
public:
  // A wrapper to retain proof source details which has filter chain.
  class EnvoyProcessClientHelloResultCallback : public quic::ProcessClientHelloResultCallback {
  public:
    EnvoyProcessClientHelloResultCallback(
        EnvoyQuicCryptoServerStream* parent,
        std::unique_ptr<quic::ProcessClientHelloResultCallback> done_cb)
        : parent_(parent), done_cb_(std::move(done_cb)) {}

    // quic::ProcessClientHelloResultCallback
    void Run(quic::QuicErrorCode error, const std::string& error_details,
             std::unique_ptr<quic::CryptoHandshakeMessage> message,
             std::unique_ptr<quic::DiversificationNonce> diversification_nonce,
             std::unique_ptr<quic::ProofSource::Details> proof_source_details) override;

    void cancel() { parent_ = nullptr; }

  private:
    EnvoyQuicCryptoServerStream* parent_;
    std::unique_ptr<quic::ProcessClientHelloResultCallback> done_cb_;
  };

  EnvoyQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                              quic::QuicCompressedCertsCache* compressed_certs_cache,
                              quic::QuicSession* session,
                              quic::QuicCryptoServerStreamBase::Helper* helper)
      : quic::QuicCryptoServerStream(crypto_config, compressed_certs_cache, session, helper) {}

  ~EnvoyQuicCryptoServerStream() override;

  // quic::QuicCryptoServerStream
  // Override to retain ProofSource::Details.
  void ProcessClientHello(
      quic::QuicReferenceCountedPointer<quic::ValidateClientHelloResultCallback::Result> result,
      std::unique_ptr<quic::ProofSource::Details> proof_source_details,
      std::unique_ptr<quic::ProcessClientHelloResultCallback> done_cb) override;
  // EnvoyCryptoServerStream
  const EnvoyQuicProofSourceDetails* proofSourceDetails() const override { return details_.get(); }

private:
  EnvoyProcessClientHelloResultCallback* done_cb_wrapper_{nullptr};
  std::unique_ptr<EnvoyQuicProofSourceDetails> details_;
};

// A dedicated stream to do TLS1.3 handshake.
class EnvoyQuicTlsServerHandshaker : public quic::TlsServerHandshaker,
                                     public EnvoyCryptoServerStream {
public:
  EnvoyQuicTlsServerHandshaker(quic::QuicSession* session,
                               const quic::QuicCryptoServerConfig& crypto_config)
      : quic::TlsServerHandshaker(session, crypto_config) {}

  // EnvoyCryptoServerStream
  const EnvoyQuicProofSourceDetails* proofSourceDetails() const override {
    return dynamic_cast<EnvoyQuicProofSourceDetails*>(proof_source_details());
  }
};

} // namespace Quic
} // namespace Envoy
