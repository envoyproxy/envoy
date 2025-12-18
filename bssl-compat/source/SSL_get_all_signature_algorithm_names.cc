#include <openssl/ssl.h>
#include <ossl.h>

#include <string>
#include <vector>

static const char* kSigAlgCandidates[] = {"ecdsa_secp256r1_sha256",
                                          "ecdsa_secp384r1_sha384",
                                          "ecdsa_secp521r1_sha512",
                                          "ed25519",
                                          "ed448",
                                          "rsa_pss_pss_sha256",
                                          "rsa_pss_pss_sha384",
                                          "rsa_pss_pss_sha512",
                                          "rsa_pss_rsae_sha256",
                                          "rsa_pss_rsae_sha384",
                                          "rsa_pss_rsae_sha512",
                                          "rsa_pkcs1_sha256",
                                          "rsa_pkcs1_sha384",
                                          "rsa_pkcs1_sha512",
                                          "ecdsa_sha224",
                                          "ecdsa_sha256",
                                          "ecdsa_sha384",
                                          "ecdsa_sha512",
                                          "ecdsa_sha1",
                                          "rsa_pkcs1_sha224",
                                          "rsa_pkcs1_sha1",
                                          "dsa_sha224",
                                          "dsa_sha1",
                                          "dsa_sha256",
                                          "dsa_sha384",
                                          "dsa_sha512",
                                          "gostr34102012_256_intrinsic",
                                          "gostr34102012_512_intrinsic",
                                          "gostr34102012_256_gostr34112012_256",
                                          "gostr34102012_512_gostr34112012_512",
                                          "gostr34102001_gostr3411",
                                          "rsa_pkcs1_md5_sha1",
                                          "rsa_pkcs1_sha256_legacy"};

#define CANDIDATES_SIZE (sizeof(kSigAlgCandidates) / sizeof(kSigAlgCandidates[0]))

static std::vector<std::string> init_all_signature_algorithm_names() {
  std::vector<std::string> names;

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
  if (ctx) {
    bssl::UniquePtr<SSL> ssl(SSL_new(ctx.get()));
    if (ssl) {
      for (size_t i = 0; i < CANDIDATES_SIZE; ++i) {
        const char* candidate = kSigAlgCandidates[i];
        if (ossl.ossl_SSL_set1_sigalgs_list(ssl.get(), candidate)) {
          names.push_back(candidate);
        }
      }
    }
  }

  return names;
}

size_t SSL_get_all_signature_algorithm_names(const char** out, size_t max_out) {
  static std::vector<std::string> validSigAlgs = init_all_signature_algorithm_names();

  if (max_out == 0 || out == nullptr) {
    return validSigAlgs.size();
  }

  for (size_t i = 0; i < validSigAlgs.size() && i < max_out; i++) {
    out[i] = validSigAlgs[i].c_str();
  }

  return validSigAlgs.size();
}
