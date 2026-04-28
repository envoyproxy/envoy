#include <openssl/ssl.h>
#include <ossl.h>
#include "log.h"


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L4390
 *
 * https://www.openssl.org/docs/man3.0/man3/SSL_get_peer_signature_nid.html
 * https://www.openssl.org/docs/man3.0/man3/SSL_get_peer_signature_type_nid.html
 */
uint16_t SSL_get_peer_signature_algorithm(const SSL *ssl) {
  int peer_sig_dgst_nid;
  int peer_sig_pkey_nid;

  if(!ossl.ossl_SSL_get_peer_signature_nid((SSL*)ssl, &peer_sig_dgst_nid)) {
    return 0;
  }

  if(!ossl.ossl_SSL_get_peer_signature_type_nid((SSL*)ssl, &peer_sig_pkey_nid)) {
    return 0;
  }

  static const struct {
    int pkey_nid;
    int dgst_nid;
    uint16_t sigalg;
  }
  sigalgs[] = {
    { NID_rsaEncryption, NID_sha1,   SSL_SIGN_RSA_PKCS1_SHA1   },
    { NID_rsaEncryption, NID_sha256, SSL_SIGN_RSA_PKCS1_SHA256 },
    { NID_rsaEncryption, NID_sha384, SSL_SIGN_RSA_PKCS1_SHA384 },
    { NID_rsaEncryption, NID_sha512, SSL_SIGN_RSA_PKCS1_SHA512 },

    { NID_rsassaPss, NID_sha256, SSL_SIGN_RSA_PSS_RSAE_SHA256 },
    { NID_rsassaPss, NID_sha384, SSL_SIGN_RSA_PSS_RSAE_SHA384 },
    { NID_rsassaPss, NID_sha512, SSL_SIGN_RSA_PSS_RSAE_SHA512 },

    { NID_X9_62_id_ecPublicKey, NID_sha1, SSL_SIGN_ECDSA_SHA1 },
    { NID_X9_62_id_ecPublicKey, NID_sha256, SSL_SIGN_ECDSA_SECP256R1_SHA256 },
    { NID_X9_62_id_ecPublicKey, NID_sha384, SSL_SIGN_ECDSA_SECP384R1_SHA384 },
    { NID_X9_62_id_ecPublicKey, NID_sha512, SSL_SIGN_ECDSA_SECP521R1_SHA512 },

    { NID_ED25519, NID_undef, SSL_SIGN_ED25519 },
  };

  for(int i = 0; i < (sizeof(sigalgs) / sizeof(sigalgs[0])); i++) {
    if ((sigalgs[i].dgst_nid == peer_sig_dgst_nid)) {
      if ((sigalgs[i].pkey_nid == NID_undef) || (sigalgs[i].pkey_nid == peer_sig_pkey_nid)) {
        return sigalgs[i].sigalg;
      }
    }
  }

  bssl_compat_warn("%s() : peer_sig_dgst_nid=%04x, peer_sig_pkey_nid=0x%04x",
                          __func__, peer_sig_dgst_nid, peer_sig_pkey_nid);

  return 0;
}
