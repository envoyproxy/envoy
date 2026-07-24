#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#SSL_CTX_set1_group_ids
 *
 * SSL_CTX_set1_group_ids configures the supported groups using IANA TLS
 * group IDs (uint16_t). This is the reverse mapping of SSL_get_curve_id.
 */
int SSL_CTX_set1_group_ids(SSL_CTX *ctx, const uint16_t *group_ids, size_t num_group_ids) {
  int nids[num_group_ids];
  for (size_t i = 0; i < num_group_ids; i++) {
    switch (group_ids[i]) {
    case 1: nids[i] = ossl_NID_sect163k1; break;
    case 2: nids[i] = ossl_NID_sect163r1; break;
    case 3: nids[i] = ossl_NID_sect163r2; break;
    case 4: nids[i] = ossl_NID_sect193r1; break;
    case 5: nids[i] = ossl_NID_sect193r2; break;
    case 6: nids[i] = ossl_NID_sect233k1; break;
    case 7: nids[i] = ossl_NID_sect233r1; break;
    case 8: nids[i] = ossl_NID_sect239k1; break;
    case 9: nids[i] = ossl_NID_sect283k1; break;
    case 10: nids[i] = ossl_NID_sect283r1; break;
    case 11: nids[i] = ossl_NID_sect409k1; break;
    case 12: nids[i] = ossl_NID_sect409r1; break;
    case 13: nids[i] = ossl_NID_sect571k1; break;
    case 14: nids[i] = ossl_NID_sect571r1; break;
    case 15: nids[i] = ossl_NID_secp160k1; break;
    case 16: nids[i] = ossl_NID_secp160r1; break;
    case 17: nids[i] = ossl_NID_secp160r2; break;
    case 18: nids[i] = ossl_NID_secp192k1; break;
    case 19: nids[i] = ossl_NID_X9_62_prime192v1; break;
    case 20: nids[i] = ossl_NID_secp224k1; break;
    case 21: nids[i] = ossl_NID_secp224r1; break;
    case 22: nids[i] = ossl_NID_secp256k1; break;
    case 23: nids[i] = ossl_NID_X9_62_prime256v1; break; /* SSL_GROUP_SECP256R1 */
    case 24: nids[i] = ossl_NID_secp384r1; break;        /* SSL_GROUP_SECP384R1 */
    case 25: nids[i] = ossl_NID_secp521r1; break;        /* SSL_GROUP_SECP521R1 */
    case 26: nids[i] = ossl_NID_brainpoolP256r1; break;
    case 27: nids[i] = ossl_NID_brainpoolP384r1; break;
    case 28: nids[i] = ossl_NID_brainpoolP512r1; break;
    case 29: nids[i] = ossl_NID_X25519; break;           /* SSL_GROUP_X25519 */
    case 30: nids[i] = ossl_NID_X448; break;
    case 256: nids[i] = ossl_NID_ffdhe2048; break;
    case 257: nids[i] = ossl_NID_ffdhe3072; break;
    case 258: nids[i] = ossl_NID_ffdhe4096; break;
    case 259: nids[i] = ossl_NID_ffdhe6144; break;
    case 260: nids[i] = ossl_NID_ffdhe8192; break;
    default:
      return 0;
    }
  }
  return ossl.ossl_SSL_CTX_set1_groups(ctx, nids, num_group_ids);
}
