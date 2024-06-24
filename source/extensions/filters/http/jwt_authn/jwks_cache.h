#pragma once

#include <memory>

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/extensions/filters/http/common/jwks_fetcher.h"
#include "source/extensions/filters/http/jwt_authn/jwks_async_fetcher.h"
#include "source/extensions/filters/http/jwt_authn/jwt_cache.h"
#include "source/extensions/filters/http/jwt_authn/stats.h"

#include "absl/strings/string_view.h"
#include "jwt_verify_lib/jwks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class JwksCache;
using JwksCachePtr = std::unique_ptr<JwksCache>;

using JwksConstPtr = std::unique_ptr<const ::google::jwt_verify::Jwks>;
using JwksConstSharedPtr = std::shared_ptr<const ::google::jwt_verify::Jwks>;

/**
 * Interface to access all configured Jwt rules and their cached Jwks objects.
 * It only caches Jwks specified in the config.
 * Its usage:
 *     auto jwks_cache = JwksCache::create(Config);
 *
 *     // for a given jwt
 *     auto jwks_data = jwks_cache->findByIssuer(jwt->getIssuer());
 *     if (!jwks_data->areAudiencesAllowed(jwt->getAudiences())) reject;
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
  virtual ~JwksCache() = default;

  // Interface to access a Jwks config rule and its cached Jwks object.
  class JwksData {
  public:
    virtual ~JwksData() = default;

    // Check if a list of audiences are allowed.
    virtual bool areAudiencesAllowed(const std::vector<std::string>& audiences) const PURE;

    // Check if a subject is allowed.
    virtual bool isSubjectAllowed(absl::string_view sub) const PURE;

    // Check if the current credential lifetime is allowed.
    virtual bool isLifetimeAllowed(const absl::Time& now, const absl::Time* exp) const PURE;

    // Get the cached config: JWT rule.
    virtual const envoy::extensions::filters::http::jwt_authn::v3::JwtProvider&
    getJwtProvider() const PURE;

    // Get the Jwks object.
    virtual const ::google::jwt_verify::Jwks* getJwksObj() const PURE;

    // Return true if jwks object is expired.
    virtual bool isExpired() const PURE;

    // Set a remote Jwks.
    virtual const ::google::jwt_verify::Jwks* setRemoteJwks(JwksConstPtr&& jwks) PURE;

    // Get Token Cache.
    virtual JwtCache& getJwtCache() PURE;
  };

  // If there is only one provider in the config, return the data for that provider.
  // It is only used for checking "failed_status_in_metadata" config for now.
  virtual JwksData* getSingleProvider() PURE;

  // Lookup issuer cache map. The cache only stores Jwks specified in the config.
  virtual JwksData* findByIssuer(const std::string& issuer) PURE;

  // Lookup provider cache map.
  virtual JwksData* findByProvider(const std::string& provider) PURE;

  virtual JwtAuthnFilterStats& stats() PURE;

  // Factory function to create an instance.
  static JwksCachePtr
  create(const envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication& config,
         Server::Configuration::FactoryContext& context, CreateJwksFetcherCb fetcher_fn,
         JwtAuthnFilterStats& stats);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
