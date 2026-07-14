#pragma once

#include "source/common/tls/ssl_handshaker.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

/**
 * Test helper that subclasses the real TLS handshaker implementation so dynamic casts in production
 * code succeed.
 */
class MockSslHandshakerImpl : public SslHandshakerImpl {
public:
  explicit MockSslHandshakerImpl(SSL* ssl)
      : SslHandshakerImpl(bssl::UniquePtr<SSL>(ssl), 0, nullptr), mock_ssl_(ssl) {}

  SSL* ssl() const override { return mock_ssl_; }

private:
  SSL* mock_ssl_{nullptr};
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
