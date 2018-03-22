#pragma once

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
    JwtLocation(const std::string& token, const std::set<std::string>& issuers, bool from_authorization,
          const LowerCaseString* header_name)
        : token_(token), allowed_issuers_(issuers), from_authorization_(from_authorization),
          header_name_(header_name) {}

    // Get the token string
    const std::string& token() const { return token_; }

    // Check if the issuer has specified the location.
    bool isIssuerAllowed(const std::string& issuer) const {
      return allowed_issuers_.find(issuer) != allowed_issuers_.end();
    }

    // Remove the token from the headers
    // TODO(qiwzhang): to remove token from query parameter.
    void remove(HeaderMap* headers) {
      if (from_authorization_) {
        headers->removeAuthorization();
      } else if (header_name_ != nullptr) {
        headers->remove(*header_name_);
      }
    }

  private:
    // Extracted token.
    std::string token_;
    // Stored issuers specified the location.
    const std::set<std::string>& allowed_issuers_;
    // True if token is extracted from default "Authorization" header
    // Most popular token location is "Authorization" header, handle it specially to use
    // HeaderMap O(1) access of Authentication() call.
    bool from_authorization_;
    // Not nullptr if token is extracted from other custom headers.
    const LowerCaseString* header_name_;
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
   *   from header A, but the specified location from its issuer is header B. This JWT will be discarded.
   * 
   * Usage:
   *
   *  Extractor   extractor(config);
   *  std::vector<JwtLocationPtr> tokens;
   *  extractor.extract(headers, &tokens);
   *
   *  for (const auto& token : tokens) {
   *     Jwt jwt;
   *     if (jwt.parseFromString(token.token()) != Status::Ok) // parse fails, drop it.
   *     
   *     if (!token.isIssuerAllowed(jwt.iss())) // from unspecified location, drop it
   *     
   *     if (remove_token) token.remove(headers); // remove token from headers
   *  }
   */
class Extractor : public Logger::Loggable<Logger::Id::filter> {
public:
  Extractor(const envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config);

  // Return the extracted JWT tokens.
  void extract(const HeaderMap& headers, std::vector<JwtLocationPtr>* tokens) const;

private:
  struct LowerCaseStringCmp {
    bool operator()(const LowerCaseString& lhs, const LowerCaseString& rhs) const {
      return lhs.get() < rhs.get();
    }
  };
  // The map of header to set of issuers
  std::map<LowerCaseString, std::set<std::string>, LowerCaseStringCmp> header_maps_;
  // The map of parameters to set of issuers.
  std::map<std::string, std::set<std::string>> param_maps_;
  // Special handling of Authorization header.
  std::set<std::string> authorization_issuers_;
};

} // namespace JwtAuthn
} // namespace Envoy
