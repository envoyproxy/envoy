#pragma once

#include <map>
#include <string>
#include <unordered_set>

#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"
#include "envoy/http/header_map.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * JwtLocation stores following token infomation:
 *
 * * extracted token string,
 * * the location where the JWT is extracted from,
 * * list of issuers specified the location.
 *
 */
class JwtLocation {
public:
  virtual ~JwtLocation() {}

  // Get the token string
  virtual const std::string& token() const PURE;

  // Check if an issuer has specified the location.
  virtual bool isIssuerSpecified(const std::string& issuer) const PURE;

  // Remove the token from the headers
  virtual void removeJwt(Http::HeaderMap& headers) const PURE;
};

typedef std::unique_ptr<const JwtLocation> JwtLocationConstPtr;

class Extractor;
typedef std::unique_ptr<const Extractor> ExtractorConstPtr;

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
  virtual ~Extractor() {}

  /**
   * Extract all JWT tokens from the headers. If set of header_keys or param_keys
   * is not empty only those in the matching locations wil be returned.
   *
   * @param headers is the HTTP request headers.
   * @return list of extracted Jwt location info.
   */
  virtual std::vector<JwtLocationConstPtr> extract(const Http::HeaderMap& headers) const PURE;

  /**
   * Remove headers that configured to send JWT payloads.
   *
   * @param headers is the HTTP request headers.
   */
  virtual void sanitizePayloadHeaders(Http::HeaderMap& headers) const PURE;

  /**
   * Create an instance of Extractor for a given config.
   * @param the JwtAuthentication config.
   * @return the extractor object.
   */
  static ExtractorConstPtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config);

  /**
   * Create an instance of Extractor for a given config.
   * @param from_headers header location config.
   * @param from_params query param location config.
   * @return the extractor object.
   */
  static ExtractorConstPtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider& provider);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
