#include <stddef.h>
#include <openssl/ssl.h>

extern "C" void SSL_get0_ech_name_override(const SSL* ssl, const char** out_name,
                                           size_t* out_name_len) {
  (void)ssl;
  *out_name = nullptr;
  *out_name_len = 0;
}
