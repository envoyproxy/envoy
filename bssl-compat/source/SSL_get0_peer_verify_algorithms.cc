#include <openssl/ssl.h>
#include <ossl.h>
#include "log.h"


static void freefunc(void *parent, void *ptr, CRYPTO_EX_DATA *ad, int idx, long argl, void *argp) {
  delete[] static_cast<uint16_t*>(ptr);
}

// Allocate a new "exdata" index for holding on to return arrays
static const int exindex = ossl_CRYPTO_get_ex_new_index(ossl_CRYPTO_EX_INDEX_SSL, 0, nullptr,
                                                        nullptr, nullptr, freefunc);


// SSL_get0_peer_verify_algorithms sets |*out_sigalgs| to an array containing
// the signature algorithms the peer is able to verify. It returns the length of
// the array. Note these values are only sent starting TLS 1.2 and only
// mandatory starting TLS 1.3. If not sent, the empty array is returned. For the
// historical client certificate types list, see |SSL_get0_certificate_types|.
//
// The behavior of this function is undefined except during the callbacks set by
// by |SSL_CTX_set_cert_cb| and |SSL_CTX_set_client_cert_cb| or when the
// handshake is paused because of them.
//
// Each value returned in out_sigalgs is one of BoringSSL's SSL_SIGN_*
// constants, defined in <openssl/ssl.h>:
//

// At the time of writing, this function is only used in testing, and the only
// sigalg values it is expected to report are:
//      SSL_SIGN_RSA_PSS_RSAE_SHA256
//      SSL_SIGN_RSA_PKCS1_SHA256
//      SSL_SIGN_ECDSA_SECP256R1_SHA256
//
// In order to return an array, via the out_sigalsgs parameter, without it
// leaking, we associate it the SSL object via the extdata mechanism. That way,
// it either gets deleted in this function when replacing it with a new value,
// or when the SSL object gets freed, via the freefunc callback.
OPENSSL_EXPORT size_t SSL_get0_peer_verify_algorithms(const SSL *ssl, const uint16_t **out_sigalgs) {

  // Delete the previous sigalgs array if there is one from a previous call
  uint16_t *oldsigalgs = static_cast<uint16_t*>(ossl_SSL_get_ex_data(const_cast<SSL*>(ssl), exindex));
  if (oldsigalgs) {
    if(ossl_SSL_set_ex_data(const_cast<SSL*>(ssl), exindex, nullptr) == 0) {
      return 0;
    }
    delete[] oldsigalgs;
  }

  // Calling with a zero idx and nulls will return the count
  int nsigalgs = ossl.ossl_SSL_get_sigalgs(const_cast<SSL*>(ssl), 0, nullptr, nullptr, nullptr, nullptr, nullptr);

  // Allocate the result array
  uint16_t *sigalgs = new uint16_t[nsigalgs];

  // Put the array into the SSL's exdata so it won't leak
  if(ossl_SSL_set_ex_data(const_cast<SSL*>(ssl), exindex, sigalgs) == 0) {
    delete[] sigalgs;
    return 0;
  }

  for (int i = 0; i < nsigalgs; i++) {
    int sign{-1};
    int hash{-1};
    int signhash{-1};
    unsigned char rsign{0};
    unsigned char rhash{0};

    if (ossl_SSL_get_sigalgs(const_cast<SSL*>(ssl), i, &sign, &hash, &signhash, &rsign, &rhash) == 0) {
      return 0;
    }

    if (signhash == NID_ecdsa_with_SHA256) {
      sigalgs[i] = SSL_SIGN_ECDSA_SECP256R1_SHA256;
    }
    else if (signhash == NID_sha256WithRSAEncryption) {
      sigalgs[i] = SSL_SIGN_RSA_PKCS1_SHA256;
    }
    else if ((sign == ossl_NID_rsassaPss) && (hash == ossl_NID_sha256)) {
      sigalgs[i] = SSL_SIGN_RSA_PSS_RSAE_SHA256;
    }
    else {
      bssl_compat_error("Unhandled sigalg : sign=%d, hash=%d, signhash=%d, rsign=%d, rhash=%d",
                                  sign, hash, signhash, (int)rsign, (int)rhash);
      sigalgs[i] = 0;
    }
  }

  *out_sigalgs = sigalgs;

  return nsigalgs;
}