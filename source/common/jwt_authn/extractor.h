#pragma once

#include <map>
#include <string>
#include <unordered_set>

#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"

#include "common/common/logger.h"

namespace Envoy {
namespace JwtAuthn {

/**
 * JwtLocation stores following info
 * *  extracted JWT string,
 * *  the location where the JWT is extracted from,
 * *  list of issuers specified the location. The issuer of extracted JWT must
 * match one of these
 * issuers.
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

typedef std::unique_ptr<JwtLocation> JwtLocationPtr;

/**
 * Extracts JWT from locations specified in the config.
 *
 * The rules of JWT extraction:
 * * Each issuer can specify its locations either at headers or query
 * parameters.
 * * If an issuer doesn't specify any locations, following default locations are
 * used:
 *      header:  Authorization: Bear <token>
 *      query parameter: ?access_token=<token>
 * * A JWT must be extracted from its configurated locations. For example, if a
 * JWT is extracted
 *   from header A, but its specified location in the config is header B. This
 * JWT should be
 * discarded.
 *
 * Usage example:
 *
 *  Extractor extractor(config);
 *  auto tokens = extractor.extract(headers);
 *  for (token : tokens) {
 *     Jwt jwt;
 *     if (jwt.parseFromString(token->token()) != Status::Ok) // parse fails,
 * drop it.
 *
 *     if (!token->isIssuerSpecified(jwt.iss())) // from unspecified location,
 * drop it
 *
 *     if (need_to_remove) token->removeJwt(headers); // remove the JWT from
 * headers
 *  }
 */
class Extractor {
public:
  Extractor(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config);

  /**
   * Extract all JWT tokens from the headers
   * @param headers is the HTTP request headers.
   * @return list of extracted Jwt location info.
   */
  std::vector<JwtLocationPtr> extract(const Http::HeaderMap& headers) const;

private:
  // add a header config
  void addHeaderConfig(const std::string& issuer, const Http::LowerCaseString& header_name,
                       const std::string& value_prefix);
  // add a param config
  void addParamConfig(const std::string& issuer, const std::string& param);

  // HeaderMap value type to store prefix and issuers that specified this
  // header.
  struct HeaderMapValue {
    HeaderMapValue(const Http::LowerCaseString& header) : header_(header) {}
    // The header name.
    Http::LowerCaseString header_;
    // Issuers that specified this header.
    std::unordered_set<std::string> specified_issuers_;
    // The value prefix.
    std::string value_prefix_;
  };
  // The map of (header + value_prefix) to HeaderMapValue
  std::map<std::string, HeaderMapValue> header_maps_;

  // ParamMap value type to store issuers that specified this header.
  struct ParamMapValue {
    // Issuers that specified this param.
    std::unordered_set<std::string> specified_issuers_;
  };
  // The map of parameters to set of issuers.
  std::map<std::string, ParamMapValue> param_maps_;
};

} // namespace JwtAuthn
} // namespace Envoy
