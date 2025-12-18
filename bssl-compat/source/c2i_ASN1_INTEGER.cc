#include <openssl/asn1.h>
#include <openssl/bytestring.h>
#include <openssl/crypto.h>
#include <ossl.h>


extern "C" ASN1_INTEGER *c2i_ASN1_INTEGER(ASN1_INTEGER **out, const unsigned char **inp, long len) {
  ASN1_INTEGER *result = NULL;
  CBB cbb;

  if (CBB_init(&cbb, len + 2)) {
    CBB child;

    if (CBB_add_asn1(&cbb, &child, CBS_ASN1_INTEGER) && CBB_add_bytes(&child, *inp, len) && CBB_flush(&cbb)) {
      const uint8_t *data = CBB_data(&cbb);

      if ((result = ossl.ossl_d2i_ASN1_INTEGER(out, &data, CBB_len(&cbb))) != NULL) {
        *inp += len;
      }
    }

    CBB_cleanup(&cbb);
  }

  return result;
}
