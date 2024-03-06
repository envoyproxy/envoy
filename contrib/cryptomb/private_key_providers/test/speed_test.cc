#include "source/common/common/assert.h"

#include "benchmark/benchmark.h"
#include "crypto_mb/ec_nistp256.h"
#include "crypto_mb/rsa.h"
#include "gtest/gtest.h"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/ssl.h"

namespace Envoy {

const std::string RsaKey = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEogIBAAKCAQEAtBPRFC+8WpCauAyIr1uCTSK6qtAeevEW1vRkn/KkFQX27UWS
NgU/IukTbA091BDae7HEiWSp7IA1IDbu2q4IwY9UksjF8yFVNZYifr/IzS6lbHOI
ZRxuBzQOWgn0+7WNqzylXQ4y88yVVqSsdfiB8kJHi9o5r+M/3TBOrWCu75iYJeBV
w0nhMYIYOxB0RkPqB1+5z4cgLjyZYuC6iZe+9m718J4LRHTd60lg9wtg4H7RUE3u
VgjLSNpNyvVpOW2qHq+o21gdS7xBQ3pbD619vBWeNDkvCaBp6YZw4ENhUxeg4xaZ
nOrNEKZw4HQnzklDJe1a69InQI6F2b/26VEGgQIDAQABAoIBABKTzMkBV7QcIOoF
2QAGN74PbCR9Dffu8UVBtzPNC2Jj2CKIP9o01luaofdOsmczSebi4vytlt4gJ9rn
7+I9fAfD6pyt+8XmVW0OzQY4cNXCDyzOCm8r7Knvk990EYL6KuBUhFbCRT1jiLCE
koolFfrRHaJu4+6iSg9ekW9PfxyWfAxtEp4XrrqgN4jN3Lrx1rYCZnuYp3Lb+7WI
fJC/rK6MTphUMLbPMvmUwHjFzoe7g9MZxRRY3kY3h1n3Ju1ZbaCbP0Vi/+tdgKAl
290J2MStWWJfOoTNnnOSYhWIPQUiFtuUiab7tJ90GGb1DyLbOrr6wG2awJoqF9ZM
Qwvkf/UCgYEA5dsHhxuX+cTHF6m+aGuyB0pF/cnFccTtwWz2WwiH6tldnOAIPfSi
WJU33C988KwJFmAurcW43VVVs7fxYbC6ptQluEI+/L/3Mj/3WgwZaqX00cEPkzKA
M1XbvanQAU0nGfq+ja7sZVpdbBoBUb6Bh7HFyLM3LgliT0kMQeolKXMCgYEAyI9W
tEHnkHoPjAVSSobBOqCVpTp1WGb7XoxhahjjZcTOgxucna26mUyJeHQrOPp88PJo
xxdDJU410p/tZARtFBoAa++IK9qC6QLjN23CuwhD7y6RNZsRZg0kOCg9SLj+zVj5
mrvZFf6663EpL82UZ2zUGl4L1sMhYkia0TMjYzsCgYAFHuAIDoFQOyYETO/E+8E3
kFwGz1vqsOxrBrZmSMZeYQFI4WTNnImRV6Gq8hPieLKrIPFpRaJcq+4A1vQ1rO47
kTZV6IPmtZAYOnyUMPjP+2p80cQ7D0Dz49HFY+cSYFmipodgOKljiKPUKLAm1guk
rj0tv3BXQjZCdeoj/cdeKQKBgF8u3+hWqs5/j2dVkzN5drUbR0oOT2iwHzZFC2pt
+2XuHFBOx2px6/AbSdbX0zeMccVsVlu+Z4iJ8LNQYTqpexciK/cNzCN75csuKqXA
ur1G8+7Mu++j84LqU7kvJ76exZaxVmygICv3I8DfiLt+JqNbG+KTpay8GNjrOkZ0
raPHAoGAQ1p/Qvp7DHP2qOnUB/rItEVgWECD3uPx4NOCq7Zcx7mb9p7CI6nePT5y
heHpaJIqVyaS5/LHJDwvdB1nvtlgc9xKa5d1fWhLL3dwFCa98x5PDlN/JztH8DIt
tTlD+8NECIvI+ytbzLS0PZWBYctAR2rP2qlMCGdYerdjwl8S98E=
-----END RSA PRIVATE KEY-----
)EOF";

const std::string EcdsaKey = R"EOF(
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIMpJw5U66K+DcA963b+/jZYrMrZDjaB0khHSwZte3vYCoAoGCCqGSM49
AwEHoUQDQgAELp3XvBfkVWQBOKo3ttAaJ6SUaUb8uKqCS504WXHWMO4h89F+nYtC
Ecgl8EiLXXyc86tawKjGdizcCjrKMiFo3A==
-----END EC PRIVATE KEY-----
)EOF";

bssl::UniquePtr<EVP_PKEY> makeEcdsaKey() {
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(EcdsaKey.data(), EcdsaKey.size()));
  bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());
  EC_KEY* ec = PEM_read_bio_ECPrivateKey(bio.get(), nullptr, nullptr, nullptr);
  RELEASE_ASSERT(ec != nullptr, "PEM_read_bio_ECPrivateKey failed.");
  RELEASE_ASSERT(1 == EVP_PKEY_assign_EC_KEY(key.get(), ec), "EVP_PKEY_assign_EC_KEY failed.");
  return key;
}
bssl::UniquePtr<EVP_PKEY> makeRsaKey() {
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(RsaKey.data(), RsaKey.size()));
  bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());
  RSA* rsa = PEM_read_bio_RSAPrivateKey(bio.get(), nullptr, nullptr, nullptr);
  RELEASE_ASSERT(rsa != nullptr, "PEM_read_bio_RSAPrivateKey failed.");
  RELEASE_ASSERT(1 == EVP_PKEY_assign_RSA(key.get(), rsa), "EVP_PKEY_assign_RSA failed.");
  return key;
}

static constexpr size_t in_len_ = 32;
static constexpr uint8_t in_[in_len_] = {0x7f};
static constexpr size_t max_out_len_ = 256;

int calculateDigest(const EVP_MD* md, const uint8_t* in, size_t in_len, unsigned char* hash,
                    unsigned int* hash_len) {
  bssl::ScopedEVP_MD_CTX ctx;

  // Calculate the message digest for signing.
  if (!EVP_DigestInit_ex(ctx.get(), md, nullptr) || !EVP_DigestUpdate(ctx.get(), in, in_len) ||
      !EVP_DigestFinal_ex(ctx.get(), hash, hash_len)) {
    return 0;
  }
  return 1;
}

void verifyEcdsa(EC_KEY* ec_key, uint8_t* out, uint32_t out_len) {
  const EVP_MD* md = SSL_get_signature_algorithm_digest(SSL_SIGN_ECDSA_SECP256R1_SHA256);
  uint8_t hash[EVP_MAX_MD_SIZE];
  uint32_t hash_len;
  calculateDigest(md, in_, in_len_, hash, &hash_len);

  EXPECT_EQ(ECDSA_verify(0, hash, hash_len, out, out_len, ec_key), 1);
}
void verifyRsa(RSA* rsa, uint8_t* out, uint32_t out_len) {
  const EVP_MD* md = SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PSS_SHA256);
  uint8_t buf[max_out_len_];
  uint32_t buf_len;
  calculateDigest(md, in_, in_len_, buf, &buf_len);

  EXPECT_EQ(RSA_verify_pss_mgf1(rsa, buf, buf_len, md, nullptr, -1, out, out_len), 1);
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_ECDSA_Signing(benchmark::State& state) {
  bssl::UniquePtr<EVP_PKEY> pkey = makeEcdsaKey();
  uint8_t out[512];
  uint32_t out_len;
  for (auto _ : state) { // NOLINT
    EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(pkey.get());
    const EVP_MD* md = SSL_get_signature_algorithm_digest(SSL_SIGN_ECDSA_SECP256R1_SHA256);

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    calculateDigest(md, in_, in_len_, hash, &hash_len);

    ECDSA_sign(0, hash, hash_len, out, &out_len, ec_key);
  }

  EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(pkey.get());
  verifyEcdsa(ec_key, out, out_len);

  state.counters["Requests"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_ECDSA_Signing);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_ECDSA_Signing_CryptoMB(benchmark::State& state) {
  bssl::UniquePtr<EVP_PKEY> pkey = makeEcdsaKey();
  uint8_t out[8][512];
  size_t out_len[8];
  for (auto _ : state) { // NOLINT
    std::unique_ptr<uint8_t[]> in_buf[8];
    size_t sig_len[8];
    const BIGNUM* priv_key[8];
    bssl::UniquePtr<BN_CTX> ctx[8];
    BIGNUM* k[8];
    for (int i = 0; i < 8; i++) {
      EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(pkey.get());
      const EVP_MD* md = SSL_get_signature_algorithm_digest(SSL_SIGN_ECDSA_SECP256R1_SHA256);

      unsigned char hash[EVP_MAX_MD_SIZE];
      unsigned int hash_len;
      calculateDigest(md, in_, in_len_, hash, &hash_len);

      priv_key[i] = EC_KEY_get0_private_key(ec_key);
      const EC_GROUP* group = EC_KEY_get0_group(ec_key);
      const BIGNUM* order = EC_GROUP_get0_order(group);
      ctx[i] = bssl::UniquePtr<BN_CTX>(BN_CTX_new());
      BN_CTX_start(ctx[i].get());
      k[i] = BN_CTX_get(ctx[i].get());
      while (BN_is_zero(k[i])) {
        BN_rand_range(k[i], order);
      }

      int len = BN_num_bits(order);
      size_t buf_len = (len + 7) / 8;
      if (8 * hash_len < static_cast<unsigned long>(len)) {
        in_buf[i] = std::make_unique<uint8_t[]>(buf_len);
        memcpy(in_buf[i].get() + buf_len - hash_len, hash, hash_len);
      } else {
        in_buf[i] = std::make_unique<uint8_t[]>(hash_len);
        memcpy(in_buf[i].get(), hash, hash_len);
      }
      sig_len[i] = ECDSA_size(ec_key);
    }

    uint8_t sig_r[8][32];
    uint8_t sig_s[8][32];
    uint8_t* pa_sig_r[8];
    uint8_t* pa_sig_s[8];
    const uint8_t* digest[8];
    for (int i = 0; i < 8; i++) {
      pa_sig_r[i] = sig_r[i];
      pa_sig_s[i] = sig_s[i];
      digest[i] = in_buf[i].get();
    }
    mbx_nistp256_ecdsa_sign_ssl_mb8(pa_sig_r, pa_sig_s, digest, k, priv_key, nullptr);

    for (int i = 0; i < 8; i++) {
      ECDSA_SIG* sig = ECDSA_SIG_new();
      BIGNUM* cur_sig_r = BN_bin2bn(sig_r[i], 32, nullptr);
      BIGNUM* cur_sig_s = BN_bin2bn(sig_s[i], 32, nullptr);
      ECDSA_SIG_set0(sig, cur_sig_r, cur_sig_s);

      CBB cbb;
      CBB_init_fixed(&cbb, out[i], sig_len[i]);
      ECDSA_SIG_marshal(&cbb, sig);
      CBB_finish(&cbb, nullptr, &out_len[i]);
      ECDSA_SIG_free(sig);
    }
  }

  EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(pkey.get());
  for (int i = 0; i < 8; i++) {
    verifyEcdsa(ec_key, out[i], out_len[i]);
  }

  state.counters["Requests"] =
      benchmark::Counter(state.iterations() * 8, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_ECDSA_Signing_CryptoMB);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_RSA_Signing(benchmark::State& state) {
  bssl::UniquePtr<EVP_PKEY> pkey = makeRsaKey();
  uint8_t out[512];
  size_t out_len;
  for (auto _ : state) { // NOLINT
    RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
    const EVP_MD* md = SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PSS_SHA256);

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    calculateDigest(md, in_, in_len_, hash, &hash_len);

    RSA_sign_pss_mgf1(rsa, &out_len, out, max_out_len_, hash, hash_len, md, nullptr, -1);
  }

  RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
  verifyRsa(rsa, out, out_len);

  state.counters["Requests"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_RSA_Signing);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_RSA_Signing_CryptoMB(benchmark::State& state) {
  bssl::UniquePtr<EVP_PKEY> pkey = makeRsaKey();
  uint8_t out[8][512];
  uint32_t out_len[8];
  for (auto _ : state) { // NOLINT
    std::unique_ptr<uint8_t[]> in_buf[8];
    uint8_t out_buf[8][512];
    uint32_t out_buf_len[8];
    const BIGNUM* p[8];
    const BIGNUM* q[8];
    const BIGNUM* dmp1[8];
    const BIGNUM* dmq1[8];
    const BIGNUM* iqmp[8];
    for (int i = 0; i < 8; i++) {
      RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
      const EVP_MD* md = SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PSS_SHA256);

      unsigned char hash[EVP_MAX_MD_SIZE];
      unsigned int hash_len;
      calculateDigest(md, in_, in_len_, hash, &hash_len);

      size_t msg_len = RSA_size(rsa);
      uint8_t* msg = static_cast<uint8_t*>(OPENSSL_malloc(msg_len));
      RSA_padding_add_PKCS1_PSS_mgf1(rsa, msg, hash, md, nullptr, -1);

      RSA_get0_factors(rsa, &p[i], &q[i]);
      RSA_get0_crt_params(rsa, &dmp1[i], &dmq1[i], &iqmp[i]);

      out_buf_len[i] = msg_len;
      in_buf[i] = std::make_unique<uint8_t[]>(msg_len);
      memcpy(in_buf[i].get(), msg, msg_len);
      OPENSSL_free(msg);
    }

    const uint8_t* from[8];
    uint8_t* to[8];
    for (int i = 0; i < 8; i++) {
      from[i] = in_buf[i].get();
      to[i] = out_buf[i];
    }
    mbx_rsa_private_crt_ssl_mb8(from, to, p, q, dmp1, dmq1, iqmp, out_buf_len[0] * 8);

    for (int i = 0; i < 8; i++) {
      out_len[i] = out_buf_len[i];
      memcpy(out[i], out_buf[i], out_buf_len[i]);
    }
  }

  RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
  for (int i = 0; i < 8; i++) {
    verifyRsa(rsa, out[i], out_len[i]);
  }

  state.counters["Requests"] =
      benchmark::Counter(state.iterations() * 8, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_RSA_Signing_CryptoMB);

} // namespace Envoy
