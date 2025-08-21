#pragma once

#include <string>

#include "envoy/network/filter.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "absl/strings/str_cat.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace Envoy {
namespace Quic {

// A ProofSource::Detail implementation which retains filter chain.
class EnvoyQuicProofSourceDetails : public quic::ProofSource::Details {
public:
  explicit EnvoyQuicProofSourceDetails(const Network::FilterChain& filter_chain)
      : filter_chain_(filter_chain) {}

  const Network::FilterChain& filterChain() const { return filter_chain_; }

private:
  const Network::FilterChain& filter_chain_;
};

// A partial implementation of quic::ProofSource which chooses a cipher suite according to the leaf
// cert to sign in GetProof().
class EnvoyQuicProofSourceBase : public quic::ProofSource,
                                 protected Logger::Loggable<Logger::Id::quic> {
public:
  ~EnvoyQuicProofSourceBase() override = default;

  // quic::ProofSource
  void GetProof(const quic::QuicSocketAddress& server_address,
                const quic::QuicSocketAddress& client_address, const std::string& hostname,
                const std::string& server_config, quic::QuicTransportVersion /*transport_version*/,
                absl::string_view chlo_hash,
                std::unique_ptr<quic::ProofSource::Callback> callback) override;

  TicketCrypter* GetTicketCrypter() override { return nullptr; }

  void ComputeTlsSignature(const quic::QuicSocketAddress& server_address,
                           const quic::QuicSocketAddress& client_address,
                           const std::string& hostname, uint16_t signature_algorithm,
                           absl::string_view in,
                           std::unique_ptr<quic::ProofSource::SignatureCallback> callback) override;
  absl::InlinedVector<uint16_t, 8> SupportedTlsSignatureAlgorithms() const override;

protected:
  virtual void signPayload(const quic::QuicSocketAddress& server_address,
                           const quic::QuicSocketAddress& client_address,
                           const std::string& hostname, uint16_t signature_algorithm,
                           absl::string_view in,
                           std::unique_ptr<quic::ProofSource::SignatureCallback> callback) PURE;
};

} // namespace Quic
} // namespace Envoy
