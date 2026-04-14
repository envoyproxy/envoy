#include <openssl/pool.h>
#include <openssl/mem.h>
#include "CRYPTO_BUFFER.h"


CRYPTO_BUFFER *CRYPTO_BUFFER_alloc(uint8_t **out_data, size_t len) {
  CRYPTO_BUFFER *const buf = OPENSSL_malloc(sizeof(CRYPTO_BUFFER));
  if (buf == NULL) {
    return NULL;
  }

  buf->data = OPENSSL_malloc(len);
  if (len != 0 && buf->data == NULL) {
    OPENSSL_free(buf);
    return NULL;
  }
  buf->len = len;

  *out_data = buf->data;

  return buf;
}
