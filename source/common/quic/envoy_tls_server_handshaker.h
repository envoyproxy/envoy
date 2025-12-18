#pragma once

#include <memory>

#include "envoy/network/filter.h"

#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicProofSource;

class EnvoyTlsServerHandshaker : public quic::TlsServerHandshaker {
public:
  EnvoyTlsServerHandshaker(quic::QuicSession* session,
                           const quic::QuicCryptoServerConfig* crypto_config,
                           EnvoyQuicProofSource* proof_source);

  ~EnvoyTlsServerHandshaker() override = default;

protected:
  std::unique_ptr<quic::ProofSourceHandle> MaybeCreateProofSourceHandle() override;

private:
  class EnvoyProofSourceHandle : public quic::ProofSourceHandle {
  public:
    EnvoyProofSourceHandle(EnvoyTlsServerHandshaker* handshaker,
                           EnvoyQuicProofSource* proof_source);
    ~EnvoyProofSourceHandle() override;

    void CloseHandle() override;

    quic::QuicAsyncStatus SelectCertificate(
        const quic::QuicSocketAddress& server_address,
        const quic::QuicSocketAddress& client_address,
        const quic::QuicConnectionId& original_connection_id,
        absl::string_view ssl_capabilities, const std::string& hostname,
        const SSL_CLIENT_HELLO& client_hello, const std::string& alpn,
        std::optional<std::string> alps,
        const std::vector<uint8_t>& quic_transport_params,
        const std::optional<std::vector<uint8_t>>& early_data_context,
        const quic::QuicSSLConfig& ssl_config) override;

    quic::QuicAsyncStatus ComputeSignature(const quic::QuicSocketAddress& server_address,
                                           const quic::QuicSocketAddress& client_address,
                                           const std::string& hostname,
                                           uint16_t signature_algorithm, absl::string_view in,
                                           size_t max_signature_size) override;

  protected:
    quic::ProofSourceHandleCallback* callback() override { return handshaker_; }

  private:
    class SignatureCallback;

    EnvoyTlsServerHandshaker* handshaker_;
    EnvoyQuicProofSource* proof_source_;
    SignatureCallback* signature_callback_{nullptr};
  };

  EnvoyQuicProofSource* envoy_proof_source_;
};

} // namespace Quic
} // namespace Envoy
