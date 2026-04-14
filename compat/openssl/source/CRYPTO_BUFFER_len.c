#include <openssl/pool.h>
#include "CRYPTO_BUFFER.h"


size_t CRYPTO_BUFFER_len(const CRYPTO_BUFFER *buf) {
  return buf->len;
}
