#pragma once

#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class CertValidatorUtil {
public:
  // Callback for allow_expired_certificate option
  static int ignoreCertificateExpirationCallback(int ok, X509_STORE_CTX* store_ctx);
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
