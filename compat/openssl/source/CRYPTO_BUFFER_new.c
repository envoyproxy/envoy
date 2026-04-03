#include <openssl/pool.h>
#include <openssl/mem.h>
#include "crypto/internal.h"
#include "CRYPTO_BUFFER.h"
#include "log.h"


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/pool.h#L48
 */
CRYPTO_BUFFER *CRYPTO_BUFFER_new(const uint8_t *data, size_t len, CRYPTO_BUFFER_POOL *pool) {
  if(pool) {
    bssl_compat_fatal("%s() with non-null pool not implemented", __func__);
  }

  CRYPTO_BUFFER *const buf = OPENSSL_malloc(sizeof(CRYPTO_BUFFER));
  if(buf == NULL) {
    return NULL;
  }

  OPENSSL_memset(buf, 0, sizeof(CRYPTO_BUFFER));

  buf->data = OPENSSL_memdup(data, len);
  if(len != 0 && buf->data == NULL) {
    OPENSSL_free(buf);
    return NULL;
  }

  buf->len = len;

  return buf;
}
