#pragma once

#include "envoy/ssl/private_key/private_key_callbacks.h"

#include "source/common/quic/quic_server_transport_socket_factory.h"

#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

class EnvoyTlsServerHandshaker : public quic::TlsServerHandshaker,
                                 public Envoy::Ssl::PrivateKeyConnectionCallbacks {
public:
  EnvoyTlsServerHandshaker(
      quic::QuicSession* session, const quic::QuicCryptoServerConfig* crypto_config,
      OptRef<const Network::DownstreamTransportSocketFactory> transport_socket_factory,
      Envoy::Event::Dispatcher& dispatcher);

  ssl_private_key_result_t PrivateKeySign(uint8_t* out, size_t* out_len, size_t max_out,
                                          uint16_t sig_alg, absl::string_view in) override;
  ssl_private_key_result_t PrivateKeyComplete(uint8_t* out, size_t* out_len,
                                              size_t max_out) override;
  void onPrivateKeyMethodComplete() override;

protected:
  void FinishHandshake() override;

private:
  OptRef<const QuicServerTransportSocketFactory> transport_socket_factory_;
};

} // namespace Quic
} // namespace Envoy
