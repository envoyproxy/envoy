#include "envoy_tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

EnvoyTlsServerHandshaker::EnvoyTlsServerHandshaker(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
    OptRef<const Network::DownstreamTransportSocketFactory> transport_socket_factory,
    Envoy::Event::Dispatcher& dispatcher)
    : quic::TlsServerHandshaker(session, crypto_config) {
  if (transport_socket_factory != absl::nullopt) {
    transport_socket_factory_.emplace(
        dynamic_cast<const QuicServerTransportSocketFactory&>(transport_socket_factory.ref()));
    for (auto cert_config : transport_socket_factory_->getTlsCertificates()) {
      if (cert_config.get().privateKeyMethod()) {
        cert_config.get().privateKeyMethod()->registerPrivateKeyMethod(GetSsl(), *this, dispatcher);
      }
    }
  }
}

ssl_private_key_result_t EnvoyTlsServerHandshaker::PrivateKeySign(uint8_t* out, size_t* out_len,
                                                                  size_t max_out, uint16_t sig_alg,
                                                                  absl::string_view in) {
  if (transport_socket_factory_->getTlsCertificates().size() > 0) {
    // TODO(soulxu): Currently the QUIC transport socket only support one certficate. After
    // QUIC transport socket with multiple certificates, we will figure out how to support
    // multiple certificates for private key provider also.
    auto private_key_method =
        transport_socket_factory_->getTlsCertificates()[0].get().privateKeyMethod();
    if (private_key_method != nullptr) {
      auto ret = private_key_method->getBoringSslPrivateKeyMethod()->sign(
          GetSsl(), out, out_len, max_out, sig_alg, reinterpret_cast<const uint8_t*>(in.data()),
          in.size());
      if (ret == ssl_private_key_retry) {
        set_expected_ssl_error(SSL_ERROR_WANT_PRIVATE_KEY_OPERATION);
      }
      return ret;
    }
  }
  return quic::TlsServerHandshaker::PrivateKeySign(out, out_len, max_out, sig_alg, in);
}

ssl_private_key_result_t EnvoyTlsServerHandshaker::PrivateKeyComplete(uint8_t* out, size_t* out_len,
                                                                      size_t max_out) {
  if (transport_socket_factory_->getTlsCertificates().size() > 0) {
    auto private_key_method =
        transport_socket_factory_->getTlsCertificates()[0].get().privateKeyMethod();
    if (private_key_method != nullptr) {
      auto ret = private_key_method->getBoringSslPrivateKeyMethod()->complete(GetSsl(), out,
                                                                              out_len, max_out);
      if (ret == ssl_private_key_success) {
        set_expected_ssl_error(SSL_ERROR_WANT_READ);
      }
      return ret;
    }
  }
  return quic::TlsServerHandshaker::PrivateKeyComplete(out, out_len, max_out);
}

void EnvoyTlsServerHandshaker::onPrivateKeyMethodComplete() { AdvanceHandshakeFromCallback(); }

void EnvoyTlsServerHandshaker::FinishHandshake() {
  quic::TlsServerHandshaker::FinishHandshake();
  if (transport_socket_factory_ != absl::nullopt) {
    for (auto cert_config : transport_socket_factory_->getTlsCertificates()) {
      if (cert_config.get().privateKeyMethod()) {
        cert_config.get().privateKeyMethod()->unregisterPrivateKeyMethod(GetSsl());
      }
    }
  }
}

} // namespace Quic
} // namespace Envoy
