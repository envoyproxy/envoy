#include <openssl/pool.h>
#include "CRYPTO_BUFFER.h"


const uint8_t *CRYPTO_BUFFER_data(const CRYPTO_BUFFER *buf) {
  return buf->data;
}
