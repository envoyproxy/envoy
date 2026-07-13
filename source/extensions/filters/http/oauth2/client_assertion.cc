#include "source/extensions/filters/http/oauth2/client_assertion.h"

#include <chrono>
#include <string>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/crypto/utility.h"
#include "source/common/json/json_sanitizer.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "openssl/bn.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

namespace {

// The algorithm is validated against the PrivateKeyJwtConfig.SigningAlgorithm enum at config time,
// so it is always one of the canonical values below and no case folding is needed here.
absl::StatusOr<absl::string_view> getHashFunction(absl::string_view algorithm) {
  if (algorithm == "RS256" || algorithm == "ES256") {
    return "sha256";
  }
  if (algorithm == "RS384" || algorithm == "ES384") {
    return "sha384";
  }
  if (algorithm == "RS512" || algorithm == "ES512") {
    return "sha512";
  }
  return absl::InvalidArgumentError(absl::StrCat("Unsupported signing algorithm: ", algorithm));
}

std::string base64UrlEncode(absl::string_view input) {
  return Base64Url::encode(input.data(), input.size());
}

// Convert a DER-encoded ECDSA signature to the raw r||s format required by JWT (RFC 7518 Section
// 3.4). EVP_DigestSign produces DER-encoded signatures, but JWT expects fixed-width big-endian
// concatenation of r and s components.
//
// The inputs are produced internally from a valid EC key (the DER comes from our own EVP_DigestSign
// and `pkey` is already verified to be an EC key by the caller), so the BoringSSL calls below
// cannot fail in practice. The RELEASE_ASSERTs document those invariants and crash rather than
// silently emit a corrupt signature if BoringSSL ever violates them.
std::vector<uint8_t> convertEcDerSignatureToRaw(const std::vector<uint8_t>& der_sig,
                                                EVP_PKEY* pkey) {
  // Parse the DER-encoded ECDSA signature.
  const uint8_t* der_ptr = der_sig.data();
  bssl::UniquePtr<ECDSA_SIG> ecdsa_sig(d2i_ECDSA_SIG(nullptr, &der_ptr, der_sig.size()));
  RELEASE_ASSERT(ecdsa_sig != nullptr, "Failed to parse DER-encoded ECDSA signature");

  const BIGNUM* r = nullptr;
  const BIGNUM* s = nullptr;
  ECDSA_SIG_get0(ecdsa_sig.get(), &r, &s);

  // Determine the component size from the EC key's curve. For EC keys, EVP_PKEY_bits returns the
  // group order in bits, which equals the degree of the curve.
  const size_t component_size = (EVP_PKEY_bits(pkey) + 7) / 8;

  // Write r and s as fixed-width big-endian integers.
  std::vector<uint8_t> raw_sig(component_size * 2);
  RELEASE_ASSERT(BN_bn2bin_padded(raw_sig.data(), component_size, r) &&
                     BN_bn2bin_padded(raw_sig.data() + component_size, component_size, s),
                 "Failed to convert ECDSA signature components to fixed-width format");

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

  // Ensure the provided key type matches the configured algorithm family. Using, e.g., an EC key
  // with an "RS*" algorithm would otherwise produce a JWT whose "alg" header does not match the
  // signature, which fails at the identity provider and is hard to diagnose.
  const int key_type = EVP_PKEY_id(pkey->getEVP_PKEY());
  const bool rsa_algorithm = absl::StartsWith(algorithm, "RS");
  if (rsa_algorithm && key_type != EVP_PKEY_RSA) {
    return absl::InvalidArgumentError(
        absl::StrCat("signing algorithm ", algorithm, " requires an RSA private key"));
  }
  if (!rsa_algorithm && key_type != EVP_PKEY_EC) {
    return absl::InvalidArgumentError(
        absl::StrCat("signing algorithm ", algorithm, " requires an EC private key"));
  }

  const auto now =
      std::chrono::duration_cast<std::chrono::seconds>(time_source.systemTime().time_since_epoch());
  const auto exp = now + lifetime;
  const std::string jti = random.uuid();

  // Sanitize strings that will be embedded in JSON to prevent injection.
  std::string client_id_buf, audience_buf, jti_buf;
  const absl::string_view safe_client_id = Json::sanitize(client_id_buf, client_id);
  const absl::string_view safe_audience = Json::sanitize(audience_buf, audience);
  const absl::string_view safe_jti = Json::sanitize(jti_buf, jti);

  // Build JWT header. The algorithm is a canonical value from PrivateKeyJwtConfig.SigningAlgorithm,
  // so it needs no sanitization.
  const std::string header = absl::StrCat(R"({"alg":")", algorithm, R"(","typ":"JWT"})");
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
    sig = convertEcDerSignatureToRaw(sig, pkey->getEVP_PKEY());
  }

  const std::string encoded_signature =
      Base64Url::encode(reinterpret_cast<const char*>(sig.data()), sig.size());

  return absl::StrCat(signing_input, ".", encoded_signature);
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
