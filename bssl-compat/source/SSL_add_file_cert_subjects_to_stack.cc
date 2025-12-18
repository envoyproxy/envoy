#include <openssl/ssl.h>
#include <ossl.h>


extern "C" int SSL_add_file_cert_subjects_to_stack(STACK_OF(X509_NAME) *out, const char *file) {
  return ossl.ossl_SSL_add_file_cert_subjects_to_stack(reinterpret_cast<ossl_STACK_OF(ossl_X509_NAME)*>(out), file);
}
