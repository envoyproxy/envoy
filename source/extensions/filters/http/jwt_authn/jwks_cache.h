
#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"

#include "jwt_verify_lib/jwks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class JwksCache;
typedef std::unique_ptr<JwksCache> JwksCachePtr;

/**
 * Interface to access all configured Jwt rules and their cached Jwks objects.
 * It only caches Jwks specified in the config.
 * Its usage:
 *     auto jwks_cache = JwksCache::create(Config);
 *
 *     // for a given jwt
 *     auto jwks_data = jwks_cache->findByIssuer(jwt->getIssuer());
 *     if (!jwks_data->isAudidenceAllowed(jwt->getAudiences())) reject;
 *
 *     if (jwks_data->getJwksObj() == nullptr || jwks_data->isExpired()) {
 *        // Fetch remote Jwks.
 *        jwks_data->setRemoteJwks(remote_jwks_str);
 *     }
 *
 *     verifyJwt(jwks_data->getJwksObj(), jwt);
 */

class JwksCache {
public:
  virtual ~JwksCache() {}

  // Interface to access a Jwks config rule and its cached Jwks object.
  class JwksData {
  public:
    virtual ~JwksData() {}

    // Get the cached config: JWT rule.
    virtual const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRule&
    getJwtRule() const PURE;

    // Check if a list of audiences are allowed.
    virtual bool areAudiencesAllowed(const std::vector<std::string>& audiences) const PURE;

    // Get the Jwks object.
    virtual const ::google::jwt_verify::Jwks* getJwksObj() const PURE;

    // Return true if jwks object is expired.
    virtual bool isExpired() const PURE;

    // Set a remote Jwks string.
    virtual ::google::jwt_verify::Status setRemoteJwks(const std::string& jwks_str) PURE;
  };

  // Lookup issuer cache map. The cache only stores Jwks specified in the config.
  virtual JwksData* findByIssuer(const std::string& name) PURE;

  // Factory function to create an instance.
  static JwksCachePtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
