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
 * JwtLocation stores following info:
 * *  extracted JWT string,
 * *  the location where the JWT is extracted from,
 * *  list of issuers specified the location.
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

/**
 * Extracts JWT from locations specified in the config.
 *
 * The rules of JWT extraction:
 * * Each issuer can specify its locations at headers and query parameters.
 *
 * * If an issuer doesn't specify any locations, following default locations are
 * used:
 *      header:  Authorization: Bear <token>
 *      query parameter: ?access_token=<token>
 *

 * Usage example:
 *
 *  auto extractor = createExtractor(config);
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
 */
class Extractor {
public:
  virtual ~Extractor() {}

  /**
   * Extract all JWT tokens from the headers
   * @param headers is the HTTP request headers.
   * @return list of extracted Jwt location info.
   */
  virtual std::vector<JwtLocationConstPtr> extract(const Http::HeaderMap& headers) const PURE;
};

typedef std::unique_ptr<const Extractor> ExtractorConstPtr;

/**
 * Create an instance of Extractor for a given config.
 * @param the JwtAuthentication config.
 * @return the extractor object.
 */
ExtractorConstPtr
createExtractor(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config);

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
