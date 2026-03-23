#pragma once

#include <chrono>
#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

inline constexpr absl::string_view kSupportedPrivateKeyJwtAlgorithms[] = {
    "RS256", "RS384", "RS512", "ES256", "ES384", "ES512"};

/**
 * Creates signed JWT client assertions for private_key_jwt authentication (RFC 7523).
 */
class ClientAssertion {
public:
  /**
   * Create a signed JWT assertion.
   * @param client_id the OAuth client ID, used as both 'iss' and 'sub' claims.
   * @param audience the token endpoint URL, used as the 'aud' claim.
   * @param private_key_pem the PEM-encoded private key for signing.
   * @param algorithm the signing algorithm (RS256, RS384, RS512, ES256, ES384, ES512).
   * @param lifetime the assertion lifetime (used to compute 'exp' from 'iat').
   * @param time_source used to get the current time for 'iat' and 'exp' claims.
   * @param random used to generate the 'jti' claim.
   * @return the signed JWT string on success, or an error status on failure.
   */
  static absl::StatusOr<std::string> create(absl::string_view client_id, absl::string_view audience,
                                            absl::string_view private_key_pem,
                                            absl::string_view algorithm,
                                            std::chrono::seconds lifetime, TimeSource& time_source,
                                            Random::RandomGenerator& random);
};

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
