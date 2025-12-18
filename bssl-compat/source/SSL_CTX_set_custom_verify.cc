#include <openssl/ssl.h>
#include <ossl.h>
#include "log.h"
#include "override.h"


/*
 * This function maps from TLS alert codes (SSL_AD_* constants) to X509 error
 * codes (X509_V_ERR_* constants).
 * 
 * Because the set of TLS alerts and X509 errors are not equal, this is not a
 * lossless mapping. However, to achieve the best mapping, this function is
 * written using some knowledge of OpenSSL's internals. Specifically, the
 * "x509table" array (in ssl/statem/statem_lic.c) was used to derive the switch
 * statement.
 * 
 * Each TLS alert case returns a corresponding X509 error that OpenSSL will map
 * back to the same TLS alert. If a TLS alert is not explicitly handled in the
 * switch, it is because OpenSSL doesn't have a mapping from any X509 value to
 * that TLS alert. In these cases, the default case will return
 * X509_V_ERR_APPLICATION_VERIFICATION which OpenSSL will map to
 * SSL_AD_HANDSHAKE_FAILURE
 * 
 * https://github.com/openssl/openssl/blob/9cff14fd97814baf8a9a07d8447960a64d616ada/ssl/statem/statem_lib.c#L1351-L1395
 */
static int tls_alert_to_x590_err(int alert)
{
  switch(alert) {
    case SSL_AD_BAD_CERTIFICATE:
      return X509_V_ERR_CERT_REJECTED;
    case SSL_AD_CERTIFICATE_EXPIRED:
      return X509_V_ERR_CERT_HAS_EXPIRED;
    case SSL_AD_CERTIFICATE_REVOKED:
      return X509_V_ERR_CERT_REVOKED;
    case SSL_AD_CERTIFICATE_UNKNOWN:
      return X509_V_ERR_INVALID_NON_CA;
    case SSL_AD_DECRYPT_ERROR:
      return X509_V_ERR_CERT_SIGNATURE_FAILURE;
    case SSL_AD_HANDSHAKE_FAILURE:
      return X509_V_ERR_APPLICATION_VERIFICATION;
    case SSL_AD_INTERNAL_ERROR:
      return X509_V_ERR_UNSPECIFIED;
    case SSL_AD_UNKNOWN_CA:
      return X509_V_ERR_INVALID_CA;
    case SSL_AD_UNSUPPORTED_CERTIFICATE:
      return X509_V_ERR_INVALID_PURPOSE;
    default:
      return X509_V_ERR_APPLICATION_VERIFICATION;
  }
}



/**
 * This is the OpenSSL callback which invokes the BoringSSL callback.
 * Return 1 to indicate verification success and 0 to indicate verification failure
 */
static int ossl_cert_verify_callback(X509_STORE_CTX *ctx, void *arg) {
  int idx {ossl_SSL_get_ex_data_X509_STORE_CTX_idx()};
  SSL *ssl {static_cast<SSL*>(ossl_X509_STORE_CTX_get_ex_data(ctx, idx))};

  // Get the SSL object from the X509_STORE_CTX
  if (ssl == nullptr) {
    bssl_compat_error("Failed to get SSL object from X509_STORE_CTX");
    return 0;
  }

  // Get correctly typed ptr to the actual SSL_CTX_set_custom_verify() callback
  auto callback {reinterpret_cast<enum ssl_verify_result_t (*)(SSL*, uint8_t*)>(arg)};
  if (callback == nullptr) {
    bssl_compat_error("NULL BoringSSL callback");
    return 0;
  }

  uint8_t alert {SSL_AD_INTERNAL_ERROR};
  enum ssl_verify_result_t verify_result;
  {
    // X509_STORE_CTX_get0_untrusted() retrieves an internal pointer to the stack of untrusted
    // certificates associated with ctx, including the peer's leaf certificate at index 0.
    // This is exactly what BoringSSL's SSL_get_peer_full_cert_chain() should return. However,
    // on OpenSSL, there is no way (that I could find) of getting that cert chain from the SSL.
    // Therefore, we use an OverrideResult<FUNC> to hold that cert chain, so that our implementation
    // of SSL_get_peer_full_cert_chain() can pick it up and return it when called.
    auto chain {reinterpret_cast<STACK_OF(X509)*>(ossl_X509_STORE_CTX_get0_untrusted(ctx))};
    OverrideResult<SSL_get_peer_full_cert_chain> override {ssl, chain};
    verify_result = callback(ssl, &alert);
  }

  switch (verify_result) {
    case ssl_verify_ok: {
      return 1;
    }
    case ssl_verify_invalid: {
      // Translate the TLS alert value, received from the BoringSSL callback, to an X509 error, and
      // set it on the X509_STORE_CTX. OpenSSL will ultimately translate the X509 error back into a
      // TLS alert value which it will send to the peer.
      ossl_X509_STORE_CTX_set_error(ctx, tls_alert_to_x590_err(alert));
      return 0;
    }
    case ssl_verify_retry: {
      // TODO: Use ossl_SSL_set_retry_verify() for client side
      // TODO: Use ossl_ASYNC_pause/start_job() for server side (or both sides)
      bssl_compat_error("Async certificate validation not supported");
      ossl_X509_STORE_CTX_set_error(ctx, X509_V_ERR_INVALID_CALL);
      return 0;
    }
  }
}

extern "C" void SSL_CTX_set_custom_verify(SSL_CTX *ctx, int mode,
                  enum ssl_verify_result_t (*callback)(SSL *ssl, uint8_t *out_alert)) {
  ossl_SSL_CTX_set_verify(ctx, mode, nullptr);
  ossl_SSL_CTX_set_cert_verify_callback(ctx, ossl_cert_verify_callback,
                                        reinterpret_cast<void*>(callback));
}
