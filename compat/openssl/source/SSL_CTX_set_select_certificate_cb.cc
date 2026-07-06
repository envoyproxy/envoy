#include <openssl/ssl.h>
#include <ossl.h>

#include "log.h"

/**
 * This is the callback type for BoringSSL's SSL_CTX_set_select_certificate_cb()
 */
typedef enum ssl_select_cert_result_t (*select_certificate_cb_t)(const SSL_CLIENT_HELLO*);

/**
 * We construct an instance of this class on the stack in a scope that surrounds
 * the invocation of the user's callback. It is then possible to use the
 * in_select_certificate_cb(ssl) function to query whether or not we are
 * executing within a SSL_CTX_set_select_certificate_cb() callback for that SSL
 * object, or not.
 *
 * This mechanism is used by SSL_get_servername() & SSL_set_ocsp_response()
 * to provide different behavior depending on invocation context.
 */
class ActiveSelectCertificateCb {
public:
  ActiveSelectCertificateCb(SSL* ssl) : ssl_(ssl) { SSL_set_ex_data(ssl_, index(), this); }
  ~ActiveSelectCertificateCb() { SSL_set_ex_data(ssl_, index(), nullptr); }
  static bool isActive(const SSL* ssl) { return SSL_get_ex_data(ssl, index()) != nullptr; }

private:
  static int index() {
    static int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return index;
  }
  SSL* ssl_;
};

/**
 * Returns true if we are currently in a SSL_CTX_set_select_certificate_cb()
 * callback invocation for the specified SSL object.
 */
bool in_select_certificate_cb(const SSL* ssl) { return ActiveSelectCertificateCb::isActive(ssl); }

/*
 * This callback function is plugged into OpenSSL using
 * ossl_SSL_CTX_set_client_hello_cb(). When it is invoked, we create an instance
 * of BoringSSL's SSL_CLIENT_HELLO struct, and fill it in the best we can, and
 * then invoke the caller's actual BoringSSL style callback function (arg),
 * passing it the SSL_CLIENT_HELLO.
 */
static int ssl_ctx_client_hello_cb(SSL* ssl, int* alert, void* arg) {
  select_certificate_cb_t callback{reinterpret_cast<select_certificate_cb_t>(arg)};

  SSL_CLIENT_HELLO client_hello;
  memset(&client_hello, 0, sizeof(client_hello));

  client_hello.ssl = ssl;
  client_hello.version = ossl.ossl_SSL_client_hello_get0_legacy_version(ssl);
  client_hello.random_len = ossl.ossl_SSL_client_hello_get0_random(ssl, &client_hello.random);
  client_hello.session_id_len =
      ossl.ossl_SSL_client_hello_get0_session_id(ssl, &client_hello.session_id);
  client_hello.cipher_suites_len =
      ossl.ossl_SSL_client_hello_get0_ciphers(ssl, &client_hello.cipher_suites);
  client_hello.compression_methods_len =
      ossl.ossl_SSL_client_hello_get0_compression_methods(ssl, &client_hello.compression_methods);

  int* extension_ids;
  size_t extension_ids_len;

  if (!ossl.ossl_SSL_client_hello_get1_extensions_present(ssl, &extension_ids,
                                                          &extension_ids_len)) {
    *alert = SSL_AD_INTERNAL_ERROR;
    return ossl_SSL_CLIENT_HELLO_ERROR;
  }

  CBB extensions;
  CBB_init(&extensions, 1024);

  for (size_t i = 0; i < extension_ids_len; i++) {
    const unsigned char* extension_data;
    size_t extension_len;

    if (!ossl.ossl_SSL_client_hello_get0_ext(ssl, extension_ids[i], &extension_data,
                                             &extension_len) ||
        !CBB_add_u16(&extensions, extension_ids[i]) || !CBB_add_u16(&extensions, extension_len) ||
        !CBB_add_bytes(&extensions, extension_data, extension_len)) {
      OPENSSL_free(extension_ids);
      CBB_cleanup(&extensions);
      *alert = SSL_AD_INTERNAL_ERROR;
      return ossl_SSL_CLIENT_HELLO_ERROR;
    }
  }

  OPENSSL_free(extension_ids);

  if (!CBB_finish(&extensions, (uint8_t**)&client_hello.extensions, &client_hello.extensions_len)) {
    CBB_cleanup(&extensions);
    *alert = SSL_AD_INTERNAL_ERROR;
    return ossl_SSL_CLIENT_HELLO_ERROR;
  }

  // Ensure extensions are freed even if the callback throws
  std::unique_ptr<uint8_t, decltype(&OPENSSL_free)> cleanup(
      const_cast<uint8_t*>(client_hello.extensions), OPENSSL_free);

  enum ssl_select_cert_result_t result;

  {
    ActiveSelectCertificateCb active(ssl);
    result = callback(&client_hello);
  }

  switch (result) {
  case ssl_select_cert_success:
    return ossl_SSL_CLIENT_HELLO_SUCCESS;
  case ssl_select_cert_retry:
    return ossl_SSL_CLIENT_HELLO_RETRY;
  case ssl_select_cert_error:
    return ossl_SSL_CLIENT_HELLO_ERROR;
  case ssl_select_cert_disable_ech: {
    // None of the Envoy code ever returns the new ssl_select_cert_disable_ech enumerator
    bssl_compat_error("Unexpected ssl_select_cert_disable_ech result from callback");
    return ossl_SSL_CLIENT_HELLO_ERROR;
  }
  };
}

extern "C" void SSL_CTX_set_select_certificate_cb(SSL_CTX* ctx, select_certificate_cb_t cb) {
  ossl_SSL_CTX_set_client_hello_cb(ctx, ssl_ctx_client_hello_cb, reinterpret_cast<void*>(cb));
}
