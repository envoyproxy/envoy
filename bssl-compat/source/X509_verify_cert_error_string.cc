#include <openssl/x509.h>
#include <ossl.h>


/**
 * This implementats some mappings only where necessary to support Envoy
 */
const char *X509_verify_cert_error_string(long err) {
  switch(err) {
    case X509_V_ERR_UNSPECIFIED: {
      return "unknown certificate verification error";
    }
    default: {
      return ossl.ossl_X509_verify_cert_error_string(err);
    }
  }
}
