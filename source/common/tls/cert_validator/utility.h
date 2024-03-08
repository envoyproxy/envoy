#pragma once

#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class CertValidatorUtil {
public:
  // Configures `store_ctx` to ignore certificate expiration.
  static void setIgnoreCertificateExpiration(X509_STORE_CTX* store_ctx);

  // Configures `store` to ignore certificate expiration.
  static void setIgnoreCertificateExpiration(X509_STORE* store);
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
