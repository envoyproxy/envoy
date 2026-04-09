#include "source/extensions/filters/http/oauth2/client_assertion.h"

#include <chrono>
#include <string>
#include <vector>

#include "source/common/common/base64.h"
#include "source/common/crypto/utility.h"
#include "source/common/json/json_sanitizer.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/ec_key.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

namespace {

absl::StatusOr<std::string> getHashFunction(absl::string_view algorithm) {
  const std::string alg(absl::AsciiStrToUpper(algorithm));
  if (alg == "RS256" || alg == "ES256") {
    return std::string("sha256");
  }
  if (alg == "RS384" || alg == "ES384") {
    return std::string("sha384");
  }
  if (alg == "RS512" || alg == "ES512") {
    return std::string("sha512");
  }
  return absl::InvalidArgumentError(absl::StrCat("Unsupported signing algorithm: ", algorithm));
}

std::string base64UrlEncode(absl::string_view input) {
  return Base64Url::encode(input.data(), input.size());
}

// Convert a DER-encoded ECDSA signature to the raw r||s format required by JWT (RFC 7518 Section
// 3.4). EVP_DigestSign produces DER-encoded signatures, but JWT expects fixed-width big-endian
// concatenation of r and s components.
absl::StatusOr<std::vector<uint8_t>> convertEcDerSignatureToRaw(const std::vector<uint8_t>& der_sig,
                                                                EVP_PKEY* pkey) {
  // Parse the DER-encoded ECDSA signature.
  const uint8_t* der_ptr = der_sig.data();
  bssl::UniquePtr<ECDSA_SIG> ecdsa_sig(d2i_ECDSA_SIG(nullptr, &der_ptr, der_sig.size()));
  if (!ecdsa_sig) {
    return absl::InternalError("Failed to parse DER-encoded ECDSA signature");
  }

  const BIGNUM* r = nullptr;
  const BIGNUM* s = nullptr;
  ECDSA_SIG_get0(ecdsa_sig.get(), &r, &s);

  // Determine the component size from the EC key's curve.
  const EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(pkey);
  if (ec_key == nullptr) {
    return absl::InternalError("Failed to get EC key from EVP_PKEY");
  }
  const EC_GROUP* group = EC_KEY_get0_group(ec_key);
  const unsigned int degree = EC_GROUP_get_degree(group);
  const size_t component_size = (degree + 7) / 8;

  // Write r and s as fixed-width big-endian integers.
  std::vector<uint8_t> raw_sig(component_size * 2);
  if (!BN_bn2bin_padded(raw_sig.data(), component_size, r) ||
      !BN_bn2bin_padded(raw_sig.data() + component_size, component_size, s)) {
    return absl::InternalError(
        "Failed to convert ECDSA signature components to fixed-width format");
  }

  return raw_sig;
}

} // namespace

absl::StatusOr<std::string>
ClientAssertion::create(absl::string_view client_id, absl::string_view audience,
                        absl::string_view private_key_pem, absl::string_view algorithm,
                        std::chrono::seconds lifetime, TimeSource& time_source,
                        Random::RandomGenerator& random) {
  const auto hash_func = getHashFunction(algorithm);
  if (!hash_func.ok()) {
    return hash_func.status();
  }

  // TODO: Consider caching the parsed private key to avoid re-parsing PEM on every token request.
  // PEM parsing only runs on token refresh/exchange (not per-request), so the overhead is
  // negligible. Caching would require thread-safe invalidation when the SDS-provided PEM changes.
  auto pkey = Common::Crypto::UtilitySingleton::get().importPrivateKeyPEM(private_key_pem);
  if (pkey == nullptr || pkey->getEVP_PKEY() == nullptr) {
    return absl::InvalidArgumentError("Failed to parse private key PEM for JWT signing");
  }

  const auto now =
      std::chrono::duration_cast<std::chrono::seconds>(time_source.systemTime().time_since_epoch());
  const auto exp = now + lifetime;
  const std::string jti = random.uuid();

  // Sanitize strings that will be embedded in JSON to prevent injection.
  std::string client_id_buf, audience_buf, jti_buf, algorithm_buf;
  const absl::string_view safe_client_id = Json::sanitize(client_id_buf, client_id);
  const absl::string_view safe_audience = Json::sanitize(audience_buf, audience);
  const absl::string_view safe_jti = Json::sanitize(jti_buf, jti);
  const absl::string_view safe_algorithm = Json::sanitize(algorithm_buf, algorithm);

  // Build JWT header.
  const std::string header = absl::StrCat(R"({"alg":")", safe_algorithm, R"(","typ":"JWT"})");
  const std::string encoded_header = base64UrlEncode(header);

  // Build JWT payload with required claims per RFC 7523 Section 3.
  const std::string payload =
      absl::StrCat(R"({"iss":")", safe_client_id, R"(","sub":")", safe_client_id, R"(","aud":")",
                   safe_audience, R"(","exp":)", exp.count(), R"(,"iat":)", now.count(),
                   R"(,"nbf":)", now.count(), R"(,"jti":")", safe_jti, R"("})");
  const std::string encoded_payload = base64UrlEncode(payload);

  // Sign header.payload.
  const std::string signing_input = absl::StrCat(encoded_header, ".", encoded_payload);
  const std::vector<uint8_t> text(signing_input.begin(), signing_input.end());

  auto signature_result =
      Common::Crypto::UtilitySingleton::get().sign(hash_func.value(), *pkey, text);
  if (!signature_result.ok()) {
    return absl::InternalError(
        absl::StrCat("Failed to sign JWT assertion: ", signature_result.status().message()));
  }

  auto sig = std::move(signature_result.value());

  // EVP_DigestSign produces DER-encoded signatures for EC keys, but JWT (RFC 7518 Section 3.4)
  // requires raw r||s format. Convert if the key is EC.
  if (EVP_PKEY_id(pkey->getEVP_PKEY()) == EVP_PKEY_EC) {
    auto raw_result = convertEcDerSignatureToRaw(sig, pkey->getEVP_PKEY());
    if (!raw_result.ok()) {
      return raw_result.status();
    }
    sig = std::move(raw_result.value());
  }

  const std::string encoded_signature =
      Base64Url::encode(reinterpret_cast<const char*>(sig.data()), sig.size());

  return absl::StrCat(signing_input, ".", encoded_signature);
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
