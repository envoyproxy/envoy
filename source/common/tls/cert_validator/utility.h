#pragma once

#include <string>
#include <vector>

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

  // Returns a list of all Subject Alternative Names from the certificate.
  static std::vector<std::string> getCertificateSans(X509* cert);
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
