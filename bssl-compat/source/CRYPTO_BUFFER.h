#ifndef BSSL_COMPAT_CRYPTO_BUFFER_H
#define BSSL_COMPAT_CRYPTO_BUFFER_H

/*
 * This is a minimal/trivial implementation of CRYPTO_BUFFER
 */
struct crypto_buffer_st {
  uint8_t *data;
  size_t len;
};

#endif // BSSL_COMPAT_CRYPTO_BUFFER_H

