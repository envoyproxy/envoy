#include "source/extensions/filters/http/oauth2/client_assertion.h"

#include <chrono>
#include <string>
#include <vector>

#include "source/common/common/base64.h"
#include "source/common/crypto/utility.h"
#include "source/common/json/json_sanitizer.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

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

  // Import the private key from PEM.
  auto pkey = Common::Crypto::UtilitySingleton::get().importPrivateKeyPEM(private_key_pem);
  if (pkey == nullptr || pkey->getEVP_PKEY() == nullptr) {
    return absl::InvalidArgumentError("Failed to parse private key PEM for JWT signing");
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

  // Build JWT header.
  const std::string header = absl::StrCat(R"({"alg":")", algorithm, R"(","typ":"JWT"})");
  const std::string encoded_header = base64UrlEncode(header);

  // Build JWT payload with required claims per RFC 7523 Section 3.
  const std::string payload = absl::StrCat(
      R"({"iss":")", safe_client_id, R"(","sub":")", safe_client_id, R"(","aud":")", safe_audience,
      R"(","exp":)", exp.count(), R"(,"iat":)", now.count(), R"(,"jti":")", safe_jti, R"("})");
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

  const auto& sig = signature_result.value();
  const std::string encoded_signature =
      Base64Url::encode(reinterpret_cast<const char*>(sig.data()), sig.size());

  return absl::StrCat(signing_input, ".", encoded_signature);
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
