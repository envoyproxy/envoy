#include <openssl/ssl.h>
#include <string>
#include <vector>


static std::vector<std::string> init_all_cipher_names() {
  static std::vector<std::string> names;

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
  if (ctx) {
    bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
    if (ssl) {
      if (STACK_OF(SSL_CIPHER)* cipherStack = SSL_get_ciphers(ssl.get())) {
        size_t sslCipherNum = sk_SSL_CIPHER_num(cipherStack);
        for (int i = 0; i < sslCipherNum; i++) {
            if (const SSL_CIPHER* cipher = sk_SSL_CIPHER_value(cipherStack, i)) {
                if (const char* cipherName = SSL_CIPHER_get_name(cipher)) {
                  names.push_back(cipherName);
                }
            }
        }
      }
    }
  }

  return names;
}

size_t SSL_get_all_cipher_names(const char **out, size_t max_out) {
  static std::vector<std::string> validCiphers = init_all_cipher_names();

  if (max_out == 0 || out == nullptr) {
    return validCiphers.size();
  }

  for (size_t i = 0; i < validCiphers.size() && i < max_out; i++) {
    out[i] = validCiphers[i].c_str();
  }

  return validCiphers.size();
}
