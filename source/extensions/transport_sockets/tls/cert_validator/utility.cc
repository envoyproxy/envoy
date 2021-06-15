#include "source/extensions/transport_sockets/tls/cert_validator/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

int CertValidatorUtil::ignoreCertificateExpirationCallback(int ok, X509_STORE_CTX* store_ctx) {
  if (!ok) {
    int err = X509_STORE_CTX_get_error(store_ctx);
    if (err == X509_V_ERR_CERT_HAS_EXPIRED || err == X509_V_ERR_CERT_NOT_YET_VALID) {
      return 1;
    }
  }
  return ok;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
