#include <openssl/asn1.h>
#include <ossl.h>


void ASN1_TIME_free(ASN1_TIME *s) {
  ossl.ossl_ASN1_TIME_free(s);
}
