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
  virtual const DetailsWithFilterChain* proofSourceDetails() const = 0;
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

    void Run(quic::QuicErrorCode error, const std::string& error_details,
             std::unique_ptr<quic::CryptoHandshakeMessage> message,
             std::unique_ptr<quic::DiversificationNonce> diversification_nonce,
             std::unique_ptr<quic::ProofSource::Details> proof_source_details) override {
      if (parent_ == nullptr) {
        return;
      }

      if (proof_source_details != nullptr) {
        // Retain a copy of the proof source details after getting filter chain.
        parent_->details_ = std::make_unique<DetailsWithFilterChain>(
            static_cast<DetailsWithFilterChain&>(*proof_source_details));
      }
      parent_->done_cb_wrapper_ = nullptr;
      parent_ = nullptr;
      done_cb_->Run(error, error_details, std::move(message), std::move(diversification_nonce),
                    std::move(proof_source_details));
    }

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

  ~EnvoyQuicCryptoServerStream() override {
    if (done_cb_wrapper_ != nullptr) {
      done_cb_wrapper_->cancel();
    }
  }

  // quic::QuicCryptoServerStream
  // Override to retain ProofSource::Details.
  void ProcessClientHello(
      quic::QuicReferenceCountedPointer<quic::ValidateClientHelloResultCallback::Result> result,
      std::unique_ptr<quic::ProofSource::Details> proof_source_details,
      std::unique_ptr<quic::ProcessClientHelloResultCallback> done_cb) override {
    auto done_cb_wrapper =
        std::make_unique<EnvoyProcessClientHelloResultCallback>(this, std::move(done_cb));
    ASSERT(done_cb_wrapper_ == nullptr);
    done_cb_wrapper_ = done_cb_wrapper.get();
    auto details = static_cast<DetailsWithFilterChain*>(proof_source_details.get());
    if (details != nullptr) {
      // Retain a copy of the old proof source details.
      details_ = std::make_unique<DetailsWithFilterChain>(*details);
    }
    quic::QuicCryptoServerStream::ProcessClientHello(result, std::move(proof_source_details),
                                                     std::move(done_cb_wrapper));
  }

  // EnvoyCryptoServerStream
  const DetailsWithFilterChain* proofSourceDetails() const override { return details_.get(); }

private:
  EnvoyProcessClientHelloResultCallback* done_cb_wrapper_{nullptr};
  std::unique_ptr<DetailsWithFilterChain> details_;
};

// A dedicated stream to do TLS1.3 handshake.
class EnvoyQuicTlsServerHandshaker : public quic::TlsServerHandshaker,
                                     public EnvoyCryptoServerStream {
public:
  EnvoyQuicTlsServerHandshaker(quic::QuicSession* session,
                               const quic::QuicCryptoServerConfig& crypto_config)
      : quic::TlsServerHandshaker(session, crypto_config) {}

  // EnvoyCryptoServerStream
  const DetailsWithFilterChain* proofSourceDetails() const override {
    return dynamic_cast<DetailsWithFilterChain*>(proof_source_details());
  }
};

} // namespace Quic
} // namespace Envoy
