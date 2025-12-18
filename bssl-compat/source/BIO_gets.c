#include <openssl/bio.h>
#include <ossl.h>


// I could not find any BoringSSL documentation for BIO_gets() behaviour when
// reading null characters. However, the BoringSSL unit tests for BIO_gets()
// explicitly check that it does not stop reading at null characters, but
// instead reads them the same as any other characters.

// The OpenSSL documentation for BIO_gets() doesn't explicitly state how it
// deals with null characters. However, it does say "On binary input there may
// be NUL characters within the string; in this case the return value (if 
// nonnegative) may give an incorrect length".

// Looking at the OpenSSL code, and tracing through it, it appears that
// BIO_gets() on a memory bio reads null characters and returns the number of
// bytes read, the same as BoringSSL. However, on a file BIO, it also reads null
// characters, but returns the result of strlen() on the bytes that were read
// i.e. it doesn't return the number of bytes read, but instead returns the 
// length only up to the first null character.

// In order to provide exactly the same behaviour as BoringSSL, we use OpenSSL's
// BIO_get_line() function, which offers the same semantics as BoringSSL's and
// returns the correct number of bytes read, including null characters.

int BIO_gets(BIO *bio, char *buf, int size) {
  if (size <= 0) {
    return 0; // Return 0 for size<=0 to match BoringSSL behavior
  }
  return ossl.ossl_BIO_get_line(bio, buf, size);
}
