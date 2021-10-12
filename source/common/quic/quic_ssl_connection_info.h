#pragma once

#include "source/extensions/transport_sockets/tls/connection_info_impl_base.h"

namespace Envoy {
namespace Quic {

class QuicSslConnectionInfo : public Extensions::TransportSockets::Tls::ConnectionInfoImplBase {
public:
  QuicSslConnectionInfo() : Extensions::TransportSockets::Tls::ConnectionInfoImplBase() {}

  // Ssl::ConnectionInfo
  bool peerCertificateValidated() const override { return cert_validated_; };
  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  SSL* ssl() const override {
    ASSERT(ssl_ != nullptr);
    return ssl_;
  }

  void setSsl(SSL* ssl) { ssl_ = ssl; }
  void onCertValidated() { cert_validated_ = true; };

private:
  SSL* ssl_{nullptr};
  bool cert_validated_{false};
};

} // namespace Quic
} // namespace Envoy
