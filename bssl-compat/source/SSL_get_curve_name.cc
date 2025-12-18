#include <openssl/ssl.h>
#include <ossl.h>

#include <string>
#include <vector>

struct KnownCurveCandidate {
  int nid;             // The OpenSSL specific NID value for the curve
  uint16_t group_id;   // TLS group ID for the curve. Defined by TLS protocol
  const char name[22]; // The human-readable name for the curve
};

static const struct KnownCurveCandidate kCurveCandidates[] = {
    {ossl_NID_sect163k1, 1, "K-163"},
    {ossl_NID_sect163r1, 2, ""},
    {ossl_NID_sect163r2, 3, "B-163"},
    {ossl_NID_sect193r1, 4, ""},
    {ossl_NID_sect193r2, 5, ""},
    {ossl_NID_sect233k1, 6, "K-233"},
    {ossl_NID_sect233r1, 7, "B-233"},
    {ossl_NID_sect239k1, 8, ""},
    {ossl_NID_sect283k1, 9, "K-283"},
    {ossl_NID_sect283r1, 10, "B-283"},
    {ossl_NID_sect409k1, 11, "K-409"},
    {ossl_NID_sect409r1, 12, "B-409"},
    {ossl_NID_sect571k1, 13, "K-571"},
    {ossl_NID_sect571r1, 14, "B-571"},
    {ossl_NID_secp160k1, 15, ""},
    {ossl_NID_secp160r1, 16, ""},
    {ossl_NID_secp160r2, 17, ""},
    {ossl_NID_secp192k1, 18, ""},
    {ossl_NID_X9_62_prime192v1, 19, "P-192"},
    {ossl_NID_secp224k1, 20, ""},
    {ossl_NID_secp224r1, SSL_GROUP_SECP224R1, "P-224"},
    {ossl_NID_secp256k1, 22, ""},
    {ossl_NID_X9_62_prime256v1, SSL_GROUP_SECP256R1, "P-256"},
    {ossl_NID_secp384r1, SSL_GROUP_SECP384R1, "P-384"},
    {ossl_NID_secp521r1, SSL_GROUP_SECP521R1, "P-521"},
    {ossl_NID_brainpoolP256r1, 26, ""},
    {ossl_NID_brainpoolP384r1, 27, ""},
    {ossl_NID_brainpoolP512r1, 28, ""},
    {ossl_NID_X25519, SSL_GROUP_X25519, "X25519"},
    {ossl_NID_X448, 30, ""},
    {ossl_NID_ffdhe2048, 256, ""},
    {ossl_NID_ffdhe3072, 257, ""},
    {ossl_NID_ffdhe4096, 258, ""},
    {ossl_NID_ffdhe6144, 259, ""},
    {ossl_NID_ffdhe8192, 260, ""},
    {ossl_NID_undef, SSL_GROUP_X25519_MLKEM768, "X25519MLKEM768"},
    {ossl_NID_undef, SSL_GROUP_X25519_KYBER768_DRAFT00, "X25519Kyber768Draft00"},
};

#define CANDIDATES_SIZE (sizeof(kCurveCandidates) / sizeof(kCurveCandidates[0]))

static std::vector<std::string> init_all_curve_names() {
  std::vector<std::string> names;

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
  if (ctx) {
    bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
    if (ssl) {
      for (size_t i = 0; i < CANDIDATES_SIZE; ++i) {
        if (ossl.ossl_SSL_set1_groups_list(ssl.get(), kCurveCandidates[i].name)) {
          // Success: OpenSSL knows this curve and can handle it.
          names.push_back(kCurveCandidates[i].name);
        }
      }
    }
  }

  return names;
}

size_t SSL_get_all_curve_names(const char** out, size_t max_out) {
  static std::vector<std::string> names = init_all_curve_names();

  if (out && max_out) {
    for (int i = 0; i < max_out && i < names.size(); i++) {
      out[i] = names[i].c_str();
    }
  }

  return names.size(); // Return number of curves found, not written
}

/*
 * https://boringssl.googlesource.com/boringssl/+/master/ssl/ssl_key_share.cc#451
 */
const char* SSL_get_curve_name(uint16_t curve_id) {
  for (int i = 0; i < (sizeof(kCurveCandidates) / sizeof(kCurveCandidates[0])); i++) {
    if (kCurveCandidates[i].group_id == curve_id) {
      return kCurveCandidates[i].name;
    }
  }
  return NULL;
}
