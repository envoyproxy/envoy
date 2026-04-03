#include <openssl/ssl.h>
#include <ossl.h>


struct CipherStack {
  ~CipherStack() { sk_SSL_CIPHER_free(p_); }
  STACK_OF(SSL_CIPHER)* p_ {sk_SSL_CIPHER_new_null()};
};


// BoringSSL excludes TLS 1.3 ciphers so we have to filter them out
extern "C" STACK_OF(SSL_CIPHER)* SSL_CTX_get_ciphers(const SSL_CTX* ctx) {
  STACK_OF(SSL_CIPHER) *unfiltered = reinterpret_cast<STACK_OF(SSL_CIPHER)*>(
                                          ossl.ossl_SSL_CTX_get_ciphers(ctx));
  if (unfiltered == nullptr) {
    return nullptr;
  }

  thread_local CipherStack filtered;
  sk_SSL_CIPHER_zero(filtered.p_);

  for (int i = 0; i < sk_SSL_CIPHER_num(unfiltered); i++) {
    const SSL_CIPHER* cipher = sk_SSL_CIPHER_value(unfiltered, i);
    if (SSL_CIPHER_get_min_version(cipher) < TLS1_3_VERSION) {
      sk_SSL_CIPHER_push(filtered.p_, cipher);
    }
  }

  return filtered.p_;
}
