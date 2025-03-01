#include "source/extensions/common/aws/sigv4a_key_derivation.h"

#include <openssl/ssl.h>

#include "source/common/common/logger.h"
#include "source/common/crypto/utility.h"
#include "source/extensions/common/aws/sigv4a_signer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

EC_KEY* SigV4AKeyDerivation::derivePrivateKey(absl::string_view access_key_id,
                                              absl::string_view secret_access_key) {

  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  const uint8_t key_length = 32; // AWS_CAL_ECDSA_P256
  std::vector<uint8_t> private_key_buf(key_length);

  const uint8_t access_key_length = access_key_id.length();
  const uint8_t required_fixed_input_length = 32 + access_key_length;
  std::vector<uint8_t> fixed_input(required_fixed_input_length);

  const auto secret_key =
      absl::StrCat(SigV4ASignatureConstants::SigV4ASignatureVersion, secret_access_key);

  enum SigV4AKeyDerivationResult result = AkdrNextCounter;
  uint8_t external_counter = 1;

  BIGNUM* priv_key_num;
  EC_KEY* ec_key;

  while ((result == AkdrNextCounter) &&
         (external_counter <= 254)) // MAX_KEY_DERIVATION_COUNTER_VALUE
  {
    fixed_input.clear();
    fixed_input.insert(fixed_input.begin(), {0x00, 0x00, 0x00, 0x01});
    // Workaround for asan optimization issue described here
    // https://github.com/envoyproxy/envoy/pull/34377
    absl::string_view s(SigV4ASignatureConstants::SigV4AAlgorithm);
    fixed_input.insert(fixed_input.end(), s.begin(), s.end());
    fixed_input.insert(fixed_input.end(), 0x00);
    fixed_input.insert(fixed_input.end(), access_key_id.begin(), access_key_id.end());
    fixed_input.insert(fixed_input.end(), external_counter);
    fixed_input.insert(fixed_input.end(), {0x00, 0x00, 0x01, 0x00});

    auto k0 = crypto_util.getSha256Hmac(
        std::vector<uint8_t>(secret_key.begin(), secret_key.end()),
        absl::string_view(reinterpret_cast<char*>(fixed_input.data()), fixed_input.size()));

    // ECDSA q - 2
    std::vector<uint8_t> s_n_minus_2 = {
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17,
        0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x4F,
    };

    // check that k0 < s_n_minus_2
    bool lt_result = constantTimeLessThanOrEqualTo(k0, s_n_minus_2);

    if (!lt_result) {
      // Loop if k0 >= s_n_minus_2 and the counter will cause a new hmac to be generated
      external_counter++;
    } else {
      result = SigV4AKeyDerivationResult::AkdrSuccess;
      // PrivateKey d = c+1
      constantTimeAddOne(&k0);
      priv_key_num = BN_bin2bn(k0.data(), k0.size(), nullptr);

      // Create a new OpenSSL EC_KEY by curve nid for secp256r1 (NIST P-256)
      ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);

      // And set the private key we calculated above
      if (!EC_KEY_set_private_key(ec_key, priv_key_num)) {
        ENVOY_LOG(debug, "Failed to set openssl private key");
        BN_free(priv_key_num);
        OPENSSL_free(ec_key);
        return nullptr;
      }
      BN_free(priv_key_num);
    }
  }

  if (result == SigV4AKeyDerivationResult::AkdrNextCounter) {
    ENVOY_LOG(debug, "Key derivation exceeded retries, returning no signature");
    return nullptr;
  }

  return ec_key;
}

bool SigV4AKeyDerivation::derivePublicKey(EC_KEY* ec_key) {

  const BIGNUM* priv_key_num = EC_KEY_get0_private_key(ec_key);
  const EC_GROUP* group = EC_KEY_get0_group(ec_key);
  EC_POINT* point = EC_POINT_new(group);

  EC_POINT_mul(group, point, priv_key_num, nullptr, nullptr, nullptr);

  EC_KEY_set_public_key(ec_key, point);

  EC_POINT_free(point);
  return true;
}

// code based on aws sdk key derivation constant time implementations
// https://github.com/awslabs/aws-c-auth/blob/baeffa791d9d1cf61460662a6d9ac2186aaf05df/source/key_derivation.c#L152

bool SigV4AKeyDerivation::constantTimeLessThanOrEqualTo(std::vector<uint8_t> lhs_raw_be_bigint,
                                                        std::vector<uint8_t> rhs_raw_be_bigint) {

  volatile uint8_t gt = 0;
  volatile uint8_t eq = 1;

  for (uint8_t i = 0; i < lhs_raw_be_bigint.size(); ++i) {
    volatile int32_t lhs_digit = lhs_raw_be_bigint[i];
    volatile int32_t rhs_digit = rhs_raw_be_bigint[i];

    gt = gt | (((rhs_digit - lhs_digit) >> 31) & eq);
    eq = eq & ((((lhs_digit ^ rhs_digit) - 1) >> 31) & 0x01);
  }
  return (gt + gt + eq - 1) <= 0;
}

void SigV4AKeyDerivation::constantTimeAddOne(std::vector<uint8_t>* raw_be_bigint) {

  const uint8_t byte_count = raw_be_bigint->size();

  volatile uint32_t carry = 1;

  for (size_t i = 0; i < byte_count; ++i) {
    const size_t index = byte_count - i - 1;

    volatile uint32_t current_digit = (*raw_be_bigint)[index];
    current_digit = current_digit + carry;

    carry = (current_digit >> 8) & 0x01;

    (*raw_be_bigint)[index] = (current_digit & 0xFF);
  }
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
