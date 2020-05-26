#include "extensions/quic_listeners/quiche/envoy_quic_proof_source.h"

#include <openssl/bio.h>

#include "envoy/ssl/tls_certificate_config.h"

#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/quic_io_handle_wrapper.h"
#include "extensions/transport_sockets/well_known_names.h"

#include "openssl/bytestring.h"
#include "quiche/quic/core/crypto/certificate_view.h"

namespace Envoy {
namespace Quic {

quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>
EnvoyQuicProofSource::GetCertChain(const quic::QuicSocketAddress& server_address,
                                   const quic::QuicSocketAddress& client_address,
                                   const std::string& hostname) {
  absl::optional<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> cert_config_ref =
      GetTlsCertConfig(server_address, client_address, hostname);
  // Only return the first TLS cert config.
  if (!cert_config_ref.has_value()) {
    ENVOY_LOG(warn, "No matching filter chain found for handshake.");
    return quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>(
        new quic::ProofSource::Chain({}));
  }
  auto& cert_config = cert_config_ref.value().get();
  const std::string& chain_str = cert_config.certificateChain();
  std::string pem_str = std::string(const_cast<char*>(chain_str.data()), chain_str.size());
  std::stringstream pem_stream(chain_str);
  std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);
  if (chain.empty()) {
    throw EnvoyException(
        absl::StrCat("Failed to load certificate chain from ", cert_config.certificateChainPath()));
  }
  return quic::QuicReferenceCountedPointer<quic::ProofSource::Chain>(
      new quic::ProofSource::Chain(chain));
}

void EnvoyQuicProofSource::ComputeTlsSignature(
    const quic::QuicSocketAddress& server_address, const quic::QuicSocketAddress& client_address,
    const std::string& hostname, uint16_t signature_algorithm, quiche::QuicheStringPiece in,
    std::unique_ptr<quic::ProofSource::SignatureCallback> callback) {
  absl::optional<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> cert_config_ref =
      GetTlsCertConfig(server_address, client_address, hostname);
  if (!cert_config_ref.has_value()) {
    ENVOY_LOG(warn, "No matching filter chain found for handshake.");
    callback->Run(false, "", nullptr);
    return;
  }
  auto& cert_config = cert_config_ref.value().get();
  // Load private key.
  const std::string& pkey = cert_config.privateKey();
  std::stringstream pem_str(pkey);
  std::unique_ptr<quic::CertificatePrivateKey> pem_key =
      quic::CertificatePrivateKey::LoadPemFromStream(&pem_str);
  /*
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(const_cast<char*>(cert_config.privateKey().data()),
                                           cert_config.privateKey().size()));
  RELEASE_ASSERT(bio != nullptr, "");
  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(
      bio.get(), nullptr, nullptr,
      !cert_config.password().empty() ? const_cast<char*>(cert_config.password().c_str())
                                      : nullptr));
*/

  // Sign.
  std::string sig = pem_key->Sign(in, signature_algorithm);

  if (sig.empty()) {
    callback->Run(false, sig, nullptr);
  } else {
    callback->Run(true, sig, nullptr);
  }
  /*
  bssl::ScopedEVP_MD_CTX sign_context;
  EVP_PKEY_CTX* pkey_ctx;
  size_t siglen;
  std::string sig;
  if (!EVP_DigestSignInit(sign_context.get(), &pkey_ctx, EVP_sha256(), nullptr, pkey.get()) ||
      !EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) ||
      !EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, -1) ||
      !EVP_DigestSignUpdate(sign_context.get(), reinterpret_cast<const uint8_t*>(in.data()),
                            in.size()) ||
      !EVP_DigestSignFinal(sign_context.get(), nullptr, &siglen)) {
    callback->Run(false, sig, nullptr);
    return;
  }
  sig.resize(siglen);
  if (!EVP_DigestSignFinal(sign_context.get(),
                           reinterpret_cast<uint8_t*>(const_cast<char*>(sig.data())), &siglen)) {
    callback->Run(false, sig, nullptr);
    return;
  }
  sig.resize(siglen);

  callback->Run(true, sig, nullptr);
  */
}

absl::optional<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
EnvoyQuicProofSource::GetTlsCertConfig(const quic::QuicSocketAddress& server_address,
                                       const quic::QuicSocketAddress& client_address,
                                       const std::string& hostname) {
  ENVOY_LOG(trace, "Getting cert chain for {}", hostname);
  Network::ConnectionSocketImpl connection_socket(
      std::make_unique<QuicIoHandleWrapper>(listen_socket_->ioHandle()),
      quicAddressToEnvoyAddressInstance(server_address),
      quicAddressToEnvoyAddressInstance(client_address));

  connection_socket.setDetectedTransportProtocol(
      Extensions::TransportSockets::TransportProtocolNames::get().Quic);
  connection_socket.setRequestedServerName(hostname);
  connection_socket.setRequestedApplicationProtocols({"h2"});
  const Network::FilterChain* filter_chain =
      filter_chain_manager_.findFilterChain(connection_socket);
  if (filter_chain == nullptr) {
    ENVOY_LOG(warn, "No matching filter chain found for handshake.");
    return absl::nullopt;
  }
  const Network::TransportSocketFactory& transport_socket_factory =
      filter_chain->transportSocketFactory();
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> tls_cert_configs =
      dynamic_cast<const QuicServerTransportSocketFactory&>(transport_socket_factory)
          .serverContextConfig()
          .tlsCertificates();

  // Only return the first TLS cert config.
  return absl::optional<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>(
      tls_cert_configs[0].get());
}

} // namespace Quic
} // namespace Envoy
