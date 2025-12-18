#include <openssl/rsa.h>
#include <ossl.h>


extern "C" int RSA_check_key(const RSA *key) {

  BIGNUM *p=NULL, *q=NULL, *d=NULL, *e=NULL, *n=NULL;
  ossl.ossl_RSA_get0_key(key, (const BIGNUM **)&n, (const BIGNUM **)&e, (const BIGNUM **)&d);
  ossl.ossl_RSA_get0_factors(key, (const BIGNUM **)&p, (const BIGNUM **)&q);
  if((p == NULL || q == NULL || d == NULL) &&
     (n != NULL && e != NULL)) {
	  // Workaround for a mismatch with BoringSSL
	  // in case of Public Key
	  return 1;
  }

  return ossl.ossl_RSA_check_key( key );
}
