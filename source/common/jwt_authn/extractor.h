#pragma once

#include <map>
#include <set>
#include <string>

#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"

#include "common/common/logger.h"

namespace Envoy {
namespace JwtAuthn {

/**
 * Store extracted JWT string and its location where it is extracted from.
 * It has a list of issuers specified the location. Only JWT from one of these issuers are allowed
 */
class JwtLocation {
public:
  JwtLocation(const std::string& token, const std::set<std::string>& issuers,
              const Http::LowerCaseString* header_name)
      : token_(token), specified_issuers_(issuers), header_name_(header_name) {}

  // Get the token string
  const std::string& token() const { return token_; }

  // Check if the issuer has specified the location.
  bool isIssuerSpecified(const std::string& issuer) const {
    return specified_issuers_.find(issuer) != specified_issuers_.end();
  }

  // Remove the token from the headers
  void remove(Http::HeaderMap* headers) {
    // TODO(qiwzhang): to remove token from query parameter.
    if (header_name_ != nullptr) {
      headers->remove(*header_name_);
    }
  }

private:
  // Extracted token.
  std::string token_;
  // Stored issuers specified the location.
  const std::set<std::string>& specified_issuers_;
  // Not nullptr if Jwt is extracted from the header.
  const Http::LowerCaseString* header_name_;
};

typedef std::unique_ptr<JwtLocation> JwtLocationPtr;

/**
 * Extracts JWT from locations specified in the config.
 *
 * The rules of JWT extraction:
 * * Each issuer can specify its locations either at headers or query parameters.
 * * If an issuer doesn't specify any locations, following default locations are used:
 *      header:  Authorization: Bear <token>
 *      query parameter: ?access_token=<token>
 * * A JWT must be extracted from its configurated locations. For example, a JWT is extracted
 *   from header A, but the specified location from its issuer is header B. This JWT will be
 * discarded.
 *
 * Usage:
 *
 *  Extractor extractor(config);
 *
 *  std::vector<JwtLocationPtr> tokens;
 *  extractor.extract(headers, &tokens);
 *
 *  for (const auto& token : tokens) {
 *     Jwt jwt;
 *     if (jwt.parseFromString(token.token()) != Status::Ok) // parse fails, drop it.
 *
 *     if (!token.isIssuerSpecified(jwt.iss())) // from unspecified location, drop it
 *
 *     if (remove_token) token.remove(headers); // remove token from headers
 *  }
 */
class Extractor : public Logger::Loggable<Logger::Id::filter> {
public:
  Extractor(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config);

  // Return the extracted JWT tokens.
  void extract(const Http::HeaderMap& headers, std::vector<JwtLocationPtr>* tokens) const;

private:
  // config a header
  void config_header(const std::string& issuer, const std::string& header_name,
                     const std::string& value_prefix);
  // config a param
  void config_param(const std::string& issuer, const std::string& param);

  // LowerCaseString comparision operator in order to use it as map key.
  struct LowerCaseStringCmp {
    bool operator()(const Http::LowerCaseString& lhs, const Http::LowerCaseString& rhs) const {
      return lhs.get() < rhs.get();
    }
  };

  // HeaderMap value type to store prefix and issuers that specified this header.
  struct HeaderMapValue {
    // Issuers that specified this header.
    std::set<std::string> specified_issuers_;
    // The value prefix.
    std::string value_prefix_;
  };
  // The map of header to set of issuers
  std::map<Http::LowerCaseString, HeaderMapValue, LowerCaseStringCmp> header_maps_;

  // ParamMap value type to store issuers that specified this header.
  struct ParamMapValue {
    // Issuers that specified this param.
    std::set<std::string> specified_issuers_;
  };
  // The map of parameters to set of issuers.
  std::map<std::string, ParamMapValue> param_maps_;
};

} // namespace JwtAuthn
} // namespace Envoy
