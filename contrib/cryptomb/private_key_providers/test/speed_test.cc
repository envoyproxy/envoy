#include "source/common/common/assert.h"

#include "benchmark/benchmark.h"
#include "gtest/gtest.h"
#include "openssl/curve25519.h"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/rand.h"
#include "openssl/ssl.h"

#ifndef IPP_CRYPTO_DISABLED
#include "crypto_mb/ec_nistp256.h"
#include "crypto_mb/rsa.h"
#include "crypto_mb/x25519.h"
#endif

namespace Envoy {

constexpr absl::string_view RsaKey = R"EOF(
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

constexpr absl::string_view EcdsaKey = R"EOF(
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

const std::vector<uint8_t>& message() { CONSTRUCT_ON_FIRST_USE(std::vector<uint8_t>, 32, 127); }

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
  calculateDigest(md, message().data(), message().size(), hash, &hash_len);

  EXPECT_EQ(ECDSA_verify(0, hash, hash_len, out, out_len, ec_key), 1);
}

void verifyRsa(RSA* rsa, uint8_t* out, uint32_t out_len) {
  const EVP_MD* md = SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PSS_SHA256);
  uint8_t buf[32];
  uint32_t buf_len;
  calculateDigest(md, message().data(), message().size(), buf, &buf_len);

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
    calculateDigest(md, message().data(), message().size(), hash, &hash_len);

    ECDSA_sign(0, hash, hash_len, out, &out_len, ec_key);
  }

  EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(pkey.get());
  verifyEcdsa(ec_key, out, out_len);

  state.counters["Requests"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_ECDSA_Signing);

#ifndef IPP_CRYPTO_DISABLED
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
      calculateDigest(md, message().data(), message().size(), hash, &hash_len);

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
#endif

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
    calculateDigest(md, message().data(), message().size(), hash, &hash_len);

    RSA_sign_pss_mgf1(rsa, &out_len, out, 256, hash, hash_len, md, nullptr, -1);
  }

  RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
  verifyRsa(rsa, out, out_len);

  state.counters["Requests"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_RSA_Signing);

#ifndef IPP_CRYPTO_DISABLED
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_RSA_Signing_CryptoMB(benchmark::State& state) {
  bssl::UniquePtr<EVP_PKEY> pkey = makeRsaKey();
  uint8_t out[8][512];
  uint32_t out_len[8];
  for (auto _ : state) { // NOLINT
    std::unique_ptr<uint8_t[]> in_buf[8];
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
      calculateDigest(md, message().data(), message().size(), hash, &hash_len);

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

    uint8_t out_buf[8][512];
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
#endif

const std::vector<uint8_t>& x25519Key() {
  CONSTRUCT_ON_FIRST_USE(std::vector<uint8_t>,
                         {143, 108, 246, 59,  152, 5,   67,  152, 220, 11, 144,
                          198, 61,  43,  240, 209, 94,  190, 231, 111, 57, 42,
                          141, 225, 230, 220, 231, 252, 50,  205, 236, 181});
}

const std::vector<uint8_t>& x25519PeerKey() {
  CONSTRUCT_ON_FIRST_USE(std::vector<uint8_t>,
                         {64,  109, 230, 49,  46,  217, 20,  68,  22,  16, 129,
                          224, 53,  100, 61,  184, 204, 65,  60,  10,  60, 73,
                          62,  45,  192, 238, 74,  116, 237, 230, 155, 63});
}

const std::vector<uint8_t>& p256Key() {
  CONSTRUCT_ON_FIRST_USE(std::vector<uint8_t>,
                         {154, 198, 171, 184, 138, 154, 154, 228, 142, 151, 103,
                          249, 233, 0,   68,  110, 166, 26,  232, 232, 71,  127,
                          53,  107, 249, 233, 71,  125, 136, 239, 141, 143});
}

const std::vector<uint8_t>& p256PeerKey() {
  CONSTRUCT_ON_FIRST_USE(std::vector<uint8_t>,
                         {4,   0,   125, 68,  149, 141, 169, 88,  123, 178, 63,  48,  93,
                          53,  234, 36,  240, 255, 93,  0,   165, 216, 140, 3,   12,  220,
                          201, 27,  126, 171, 36,  172, 205, 175, 174, 17,  128, 214, 28,
                          189, 58,  138, 133, 149, 148, 84,  2,   46,  144, 172, 236, 7,
                          226, 234, 110, 168, 52,  119, 85,  146, 77,  157, 59,  39,  122});
}

void verifyX25519(uint8_t* ciphertext, uint8_t* secret) {
  uint8_t peer_secret[32];
  X25519(peer_secret, x25519Key().data(), ciphertext);

  EXPECT_EQ(CRYPTO_memcmp(secret, peer_secret, 32), 0);
}

void verifyP256(uint8_t* ciphertext, uint8_t* secret) {
  bssl::UniquePtr<BIGNUM> key(BN_new());
  BN_bin2bn(p256Key().data(), p256Key().size(), key.get());

  const EC_GROUP* group = EC_group_p256();
  bssl::UniquePtr<EC_POINT> point(EC_POINT_new(group));
  EC_POINT_oct2point(group, point.get(), ciphertext, 65, nullptr);
  bssl::UniquePtr<EC_POINT> result(EC_POINT_new(group));
  bssl::UniquePtr<BIGNUM> x(BN_new());
  EC_POINT_mul(group, result.get(), nullptr, point.get(), key.get(), nullptr);
  EC_POINT_get_affine_coordinates_GFp(group, result.get(), x.get(), nullptr, nullptr);

  uint8_t peer_secret[32];
  BN_bn2bin_padded(peer_secret, 32, x.get());

  EXPECT_EQ(CRYPTO_memcmp(secret, peer_secret, 32), 0);
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_X25519_Computing(benchmark::State& state) {
  uint8_t ciphertext[32];
  uint8_t secret[32];
  for (auto _ : state) { // NOLINT
    uint8_t priv_key[32];
    uint8_t public_key[32];
    X25519_keypair(public_key, priv_key);
    memcpy(ciphertext, public_key, 32);

    X25519(secret, priv_key, x25519PeerKey().data());
  }

  verifyX25519(ciphertext, secret);

  state.counters["Requests"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_X25519_Computing);

#ifndef IPP_CRYPTO_DISABLED
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_X25519_Computing_CryptoMB(benchmark::State& state) {
  uint8_t ciphertext[8][32];
  uint8_t secret[8][32];
  for (auto _ : state) { // NOLINT
    uint8_t priv_key[8][32];
    // NOLINTNEXTLINE(modernize-loop-convert)
    for (int i = 0; i < 8; i++) {
      RAND_bytes(priv_key[i], 32);
      priv_key[i][0] |= ~248;
      priv_key[i][31] &= ~64;
      priv_key[i][31] |= ~127;
    }

    uint8_t pub_key[8][32];
    const uint8_t* pa_priv_key[8];
    uint8_t* pa_pub_key[8];
    for (int i = 0; i < 8; i++) {
      pa_priv_key[i] = priv_key[i];
      pa_pub_key[i] = pub_key[i];
    }
    mbx_x25519_public_key_mb8(pa_pub_key, pa_priv_key);

    uint8_t shared_key[8][32];
    uint8_t* pa_shared_key[8];
    const uint8_t* pa_peer_key[8];
    for (int i = 0; i < 8; i++) {
      pa_shared_key[i] = shared_key[i];
      pa_peer_key[i] = x25519PeerKey().data();
    }
    mbx_x25519_mb8(pa_shared_key, pa_priv_key, pa_peer_key);

    for (int i = 0; i < 8; i++) {
      memcpy(ciphertext[i], pub_key[i], 32);
      memcpy(secret[i], pa_shared_key[i], 32);
    }
  }

  for (int i = 0; i < 8; i++) {
    verifyX25519(ciphertext[i], secret[i]);
  }

  state.counters["Requests"] =
      benchmark::Counter(state.iterations() * 8, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_X25519_Computing_CryptoMB);
#endif

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_P256_Computing(benchmark::State& state) {
  uint8_t ciphertext[65];
  uint8_t secret[32];
  for (auto _ : state) { // NOLINT
    const EC_GROUP* group = EC_group_p256();
    bssl::UniquePtr<BIGNUM> priv_key(BN_new());
    BN_rand_range_ex(priv_key.get(), 1, EC_GROUP_get0_order(group));
    bssl::UniquePtr<EC_POINT> public_key(EC_POINT_new(group));
    EC_POINT_mul(group, public_key.get(), priv_key.get(), nullptr, nullptr, nullptr);
    EC_POINT_point2oct(group, public_key.get(), POINT_CONVERSION_UNCOMPRESSED, ciphertext, 65,
                       nullptr);

    bssl::UniquePtr<EC_POINT> peer_point(EC_POINT_new(group));
    EC_POINT_oct2point(group, peer_point.get(), p256PeerKey().data(), p256PeerKey().size(),
                       nullptr);
    bssl::UniquePtr<EC_POINT> result(EC_POINT_new(group));
    bssl::UniquePtr<BIGNUM> x(BN_new());
    EC_POINT_mul(group, result.get(), nullptr, peer_point.get(), priv_key.get(), nullptr);
    EC_POINT_get_affine_coordinates_GFp(group, result.get(), x.get(), nullptr, nullptr);

    BN_bn2bin_padded(secret, 32, x.get());
  }

  verifyP256(ciphertext, secret);

  state.counters["Requests"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_P256_Computing);

#ifndef IPP_CRYPTO_DISABLED
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_P256_Computing_CryptoMB(benchmark::State& state) {
  const EC_GROUP* group = EC_group_p256();
  uint8_t ciphertext[8][65];
  uint8_t secret[8][32];
  for (auto _ : state) { // NOLINT
    bssl::UniquePtr<BIGNUM> priv_key[8];
    // NOLINTNEXTLINE(modernize-loop-convert)
    for (int i = 0; i < 8; i++) {
      priv_key[i].reset(BN_new());
      BN_rand_range_ex(priv_key[i].get(), 1, EC_GROUP_get0_order(group));
    }

    bssl::UniquePtr<BIGNUM> pub_x[8];
    bssl::UniquePtr<BIGNUM> pub_y[8];
    BIGNUM* pa_pub_x[8];
    BIGNUM* pa_pub_y[8];
    const BIGNUM* pa_priv_key[8];
    for (int i = 0; i < 8; i++) {
      pub_x[i].reset(BN_new());
      pub_y[i].reset(BN_new());
      pa_pub_x[i] = pub_x[i].get();
      pa_pub_y[i] = pub_y[i].get();
      pa_priv_key[i] = priv_key[i].get();
    }
    mbx_nistp256_ecpublic_key_ssl_mb8(pa_pub_x, pa_pub_y, nullptr, pa_priv_key, nullptr);

    uint8_t out_ciphertext[8][65];
    bssl::UniquePtr<EC_POINT> public_key[8];
    for (int i = 0; i < 8; i++) {
      public_key[i].reset(EC_POINT_new(group));
      EC_POINT_set_affine_coordinates_GFp(group, public_key[i].get(), pub_x[i].get(),
                                          pub_y[i].get(), nullptr);
      EC_POINT_point2oct(group, public_key[i].get(), POINT_CONVERSION_UNCOMPRESSED,
                         out_ciphertext[i], 65, nullptr);
    }

    bssl::UniquePtr<BIGNUM> peer_x[8];
    bssl::UniquePtr<BIGNUM> peer_y[8];
    for (int i = 0; i < 8; i++) {
      peer_x[i].reset(BN_new());
      peer_y[i].reset(BN_new());
      bssl::UniquePtr<EC_POINT> peer_key(EC_POINT_new(group));
      EC_POINT_oct2point(group, peer_key.get(), p256PeerKey().data(), p256PeerKey().size(),
                         nullptr);
      EC_POINT_get_affine_coordinates_GFp(group, peer_key.get(), peer_x[i].get(), peer_y[i].get(),
                                          nullptr);
    }

    uint8_t shared_key[8][32] = {};
    uint8_t* pa_shared_key[8];
    BIGNUM* pa_peer_x[8];
    BIGNUM* pa_peer_y[8];
    for (int i = 0; i < 8; i++) {
      pa_shared_key[i] = shared_key[i];
      pa_peer_x[i] = peer_x[i].get();
      pa_peer_y[i] = peer_y[i].get();
    }
    mbx_nistp256_ecdh_ssl_mb8(pa_shared_key, pa_priv_key, pa_peer_x, pa_peer_y, nullptr, nullptr);

    for (int i = 0; i < 8; i++) {
      memcpy(ciphertext[i], out_ciphertext[i], 65);
      memcpy(secret[i], pa_shared_key[i], 32);
    }
  }

  for (int i = 0; i < 8; i++) {
    verifyP256(ciphertext[i], secret[i]);
  }

  state.counters["Requests"] =
      benchmark::Counter(state.iterations() * 8, benchmark::Counter::kIsRate);
}
BENCHMARK(BM_P256_Computing_CryptoMB);
#endif

} // namespace Envoy
