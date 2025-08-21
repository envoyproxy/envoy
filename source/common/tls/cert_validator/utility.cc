#include "source/common/tls/cert_validator/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// When the minimum supported BoringSSL includes
// https://boringssl-review.googlesource.com/c/boringssl/+/53965, remove this and have
// callers just set `X509_V_FLAG_NO_CHECK_TIME` directly.
#if !defined(X509_V_FLAG_NO_CHECK_TIME)
namespace {
int ignoreCertificateExpirationCallback(int ok, X509_STORE_CTX* store_ctx) {
  if (!ok) {
    int err = X509_STORE_CTX_get_error(store_ctx);
    if (err == X509_V_ERR_CERT_HAS_EXPIRED || err == X509_V_ERR_CERT_NOT_YET_VALID) {
      return 1;
    }
  }
  return ok;
}
} // namespace
#endif

void CertValidatorUtil::setIgnoreCertificateExpiration(X509_STORE_CTX* store_ctx) {
#if defined(X509_V_FLAG_NO_CHECK_TIME)
  X509_STORE_CTX_set_flags(store_ctx, X509_V_FLAG_NO_CHECK_TIME);
#else
  X509_STORE_CTX_set_verify_cb(store_ctx, ignoreCertificateExpirationCallback);
#endif
}

void CertValidatorUtil::setIgnoreCertificateExpiration(X509_STORE* store) {
#if defined(X509_V_FLAG_NO_CHECK_TIME)
  X509_STORE_set_flags(store, X509_V_FLAG_NO_CHECK_TIME);
#else
  X509_STORE_set_verify_cb(store, ignoreCertificateExpirationCallback);
#endif
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
