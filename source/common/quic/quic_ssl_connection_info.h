#pragma once

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/quic_session.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "source/extensions/transport_sockets/tls/connection_info_impl_base.h"

namespace Envoy {
namespace Quic {

class QuicSslConnectionInfo : public Extensions::TransportSockets::Tls::ConnectionInfoImplBase {
public:
  QuicSslConnectionInfo(quic::QuicSession& session)
      : Extensions::TransportSockets::Tls::ConnectionInfoImplBase(), session_(session) {}

  // Ssl::ConnectionInfo
  bool peerCertificateValidated() const override { return cert_validated_; };
  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  SSL* ssl() const override {
    ASSERT(session_.GetCryptoStream() != nullptr &&
           session_.GetCryptoStream()->GetSsl() != nullptr);
    return session_.GetCryptoStream()->GetSsl();
  }

  void onCertValidated() { cert_validated_ = true; };

private:
  quic::QuicSession& session_;
  bool cert_validated_{false};
};

} // namespace Quic
} // namespace Envoy
