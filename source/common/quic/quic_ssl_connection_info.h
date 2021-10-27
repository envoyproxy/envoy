#pragma once

#include "source/extensions/transport_sockets/tls/connection_info_impl_base.h"

#include "quiche/quic/core/quic_session.h"

namespace Envoy {
namespace Quic {

// A wrapper of a QUIC session to be passed around as an indicator of ssl support and to provide
// access to the SSL object in QUIC crypto stream.
class QuicSslConnectionInfo : public Extensions::TransportSockets::Tls::ConnectionInfoImplBase {
public:
  QuicSslConnectionInfo(quic::QuicSession& session) : session_(session) {}

  // Ssl::ConnectionInfo
  bool peerCertificateValidated() const override { return cert_validated_; };
  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  SSL* ssl() const override {
    ASSERT(session_.GetCryptoStream() != nullptr);
    ASSERT(session_.GetCryptoStream()->GetSsl() != nullptr);
    return session_.GetCryptoStream()->GetSsl();
  }

  void onCertValidated() { cert_validated_ = true; };

private:
  quic::QuicSession& session_;
  bool cert_validated_{false};
};

} // namespace Quic
} // namespace Envoy
