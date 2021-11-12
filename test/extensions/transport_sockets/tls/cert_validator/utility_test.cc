#include "source/common/common/c_smart_ptr.h"
#include "source/extensions/transport_sockets/tls/cert_validator/utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

using X509StoreContextPtr = CSmartPtr<X509_STORE_CTX, X509_STORE_CTX_free>;

TEST(CertValidatorUtil, ignoreCertificateExpirationCallback) {
  // If ok = true, then true should be returned.
  EXPECT_TRUE(CertValidatorUtil::ignoreCertificateExpirationCallback(true, nullptr));

  // Expired case.
  {
    X509StoreContextPtr store_ctx = X509_STORE_CTX_new();
    X509_STORE_CTX_set_error(store_ctx.get(), X509_V_ERR_CERT_HAS_EXPIRED);
    EXPECT_TRUE(CertValidatorUtil::ignoreCertificateExpirationCallback(false, store_ctx.get()));
  }
  // Yet valid case.
  {
    X509StoreContextPtr store_ctx = X509_STORE_CTX_new();
    X509_STORE_CTX_set_error(store_ctx.get(), X509_V_ERR_CERT_NOT_YET_VALID);
    EXPECT_TRUE(CertValidatorUtil::ignoreCertificateExpirationCallback(false, store_ctx.get()));
  }
  // Other error
  {
    X509StoreContextPtr store_ctx = X509_STORE_CTX_new();
    X509_STORE_CTX_set_error(store_ctx.get(), X509_V_ERR_CERT_REVOKED);
    EXPECT_FALSE(CertValidatorUtil::ignoreCertificateExpirationCallback(false, store_ctx.get()));
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
