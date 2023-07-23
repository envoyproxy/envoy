#pragma once

#include <map>
#include <string>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * JwtLocation stores following token information:
 *
 * * extracted token string,
 * * the location where the JWT is extracted from,
 * * list of issuers specified the location.
 *
 */
class JwtLocation {
public:
  virtual ~JwtLocation() = default;

  // Get the token string
  virtual const std::string& token() const PURE;

  // Check if an issuer has specified the location.
  virtual bool isIssuerAllowed(const std::string& issuer) const PURE;

  // Remove the token from the headers
  virtual void removeJwt(Http::HeaderMap& headers) const PURE;
};

using JwtLocationConstPtr = std::unique_ptr<const JwtLocation>;
using JwtProviderList =
    std::vector<const envoy::extensions::filters::http::jwt_authn::v3::JwtProvider*>;

class Extractor;
using ExtractorConstPtr = std::unique_ptr<const Extractor>;

/**
 * Extracts JWT from locations specified in the config.
 *
 * Usage example:
 *
 *  auto extractor = Extractor::create(config);
 *  auto tokens = extractor->extract(headers);
 *  for (token : tokens) {
 *     Jwt jwt;
 *     if (jwt.parseFromString(token->token()) != Status::Ok) {
 *       // Handle JWT parsing failure.
 *     }
 *
 *     if (need_to_remove) {
 *        // remove the JWT
 *        token->removeJwt(headers);
 *     }
 *  }
 *
 */
class Extractor {
public:
  virtual ~Extractor() = default;

  /**
   * Extract all JWT tokens from the headers. If set of header_keys or param_keys
   * is not empty only those in the matching locations will be returned.
   *
   * @param headers is the HTTP request headers.
   * @return list of extracted Jwt location info.
   */
  virtual std::vector<JwtLocationConstPtr>
  extract(const Http::RequestHeaderMap& headers) const PURE;

  /**
   * Remove headers that configured to send JWT payloads and JWT claims.
   *
   * @param headers is the HTTP request headers.
   */
  virtual void sanitizeHeaders(Http::HeaderMap& headers) const PURE;

  /**
   * Create an instance of Extractor for a given config.
   * @param from_headers header location config.
   * @param from_params query param location config.
   * @return the extractor object.
   */
  static ExtractorConstPtr
  create(const envoy::extensions::filters::http::jwt_authn::v3::JwtProvider& provider);

  /**
   * Create an instance of Extractor for a list of provider config.
   * @param the list of JwtProvider configs.
   * @return the extractor object.
   */
  static ExtractorConstPtr create(const JwtProviderList& providers);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
