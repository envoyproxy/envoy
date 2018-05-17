#include "extensions/filters/http/jwt_authn/jwks_cache.h"

#include <chrono>
#include <unordered_map>

#include "common/common/logger.h"
#include "common/config/datasource.h"

using ::google::jwt_verify::Jwks;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// Default cache expiration time in 5 minutes.
constexpr int PubkeyCacheExpirationSec = 600;

// HTTP Protocol scheme prefix in JWT aud claim.
constexpr char HTTPSchemePrefix[] = "http://";
constexpr size_t HTTPSchemePrefixLen = strlen(HTTPSchemePrefix);

// HTTPS Protocol scheme prefix in JWT aud claim.
constexpr char HTTPSSchemePrefix[] = "https://";
constexpr size_t HTTPSSchemePrefixLen = strlen(HTTPSSchemePrefix);

/**
 * RFC for JWT `aud <https://tools.ietf.org/html/rfc7519#section-4.1.3>`_ only
 * specifies case sensitive comparison. But experiences showed that users
 * easily add wrong scheme and tailing slash to cause mis-match.
 * In this implemeation, scheme portion of URI and tailing slash is removed
 * before comparison.
 *
 * @param input audience string.
 * @return sanitized audience string without scheme prefix and tailing slash.
 */
std::string sanitizeAudience(const std::string& aud) {
  if (aud.empty()) {
    return aud;
  }

  size_t beg_pos = 0;
  bool sanitized = false;
  // Point beg to first character after protocol scheme prefix in audience.
  if (aud.compare(0, HTTPSchemePrefixLen, HTTPSchemePrefix) == 0) {
    beg_pos = HTTPSchemePrefixLen;
    sanitized = true;
  } else if (aud.compare(0, HTTPSSchemePrefixLen, HTTPSSchemePrefix) == 0) {
    beg_pos = HTTPSSchemePrefixLen;
    sanitized = true;
  }

  // Point end to trailing slash in aud.
  size_t end_pos = aud.length();
  if (aud[end_pos - 1] == '/') {
    --end_pos;
    sanitized = true;
  }
  if (sanitized) {
    return aud.substr(beg_pos, end_pos - beg_pos);
  }
  return aud;
}

class JwksDataImpl : public JwksCache::JwksData, public Logger::Loggable<Logger::Id::filter> {
public:
  JwksDataImpl(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRule& jwt_rule)
      : jwt_rule_(jwt_rule) {
    // Convert proto repeated fields to std::set.
    for (const auto& aud : jwt_rule_.audiences()) {
      audiences_.insert(sanitizeAudience(aud));
    }

    const auto inline_jwks = Config::DataSource::read(jwt_rule_.local_jwks(), true);
    if (!inline_jwks.empty()) {
      const Status status = setKey(inline_jwks,
                                   // inline jwks never expires.
                                   std::chrono::steady_clock::time_point::max());
      if (status != Status::Ok) {
        ENVOY_LOG(warn, "Invalid inline jwks for issuer: {}, jwks: {}", jwt_rule_.issuer(),
                  inline_jwks);
      }
    }
  }

  const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRule& getJwtRule() const override {
    return jwt_rule_;
  }

  bool areAudiencesAllowed(const std::vector<std::string>& jwt_audiences) const override {
    if (audiences_.empty()) {
      return true;
    }
    for (const auto& aud : jwt_audiences) {
      if (audiences_.find(sanitizeAudience(aud)) != audiences_.end()) {
        return true;
      }
    }
    return false;
  }

  const Jwks* getJwksObj() const override { return jwks_obj_.get(); }

  bool isExpired() const override { return std::chrono::steady_clock::now() >= expiration_time_; }

  Status setRemoteJwks(const std::string& jwks_str) override {
    return setKey(jwks_str, getRemoteJwksExpirationTime());
  }

private:
  // Get the expiration time for a remote Jwks
  std::chrono::steady_clock::time_point getRemoteJwksExpirationTime() const {
    auto expire = std::chrono::steady_clock::now();
    if (jwt_rule_.has_remote_jwks() && jwt_rule_.remote_jwks().has_cache_duration()) {
      const auto& duration = jwt_rule_.remote_jwks().cache_duration();
      expire +=
          std::chrono::seconds(duration.seconds()) + std::chrono::nanoseconds(duration.nanos());
    } else {
      expire += std::chrono::seconds(PubkeyCacheExpirationSec);
    }
    return expire;
  }

  // Set a Jwks as string.
  Status setKey(const std::string& jwks_str, std::chrono::steady_clock::time_point expire) {
    auto jwks_obj = Jwks::createFrom(jwks_str, Jwks::JWKS);
    if (jwks_obj->getStatus() != Status::Ok) {
      return jwks_obj->getStatus();
    }
    jwks_obj_ = std::move(jwks_obj);
    expiration_time_ = expire;
    return Status::Ok;
  }

  // The jwt rule config.
  const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRule& jwt_rule_;
  // Use set for fast lookup
  std::set<std::string> audiences_;
  // The generated jwks object.
  ::google::jwt_verify::JwksPtr jwks_obj_;
  // The pubkey expiration time.
  std::chrono::steady_clock::time_point expiration_time_;
};

class JwksCacheImpl : public JwksCache {
public:
  // Load the config from envoy config.
  JwksCacheImpl(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config) {
    for (const auto& rule : config.rules()) {
      jwks_data_map_.emplace(rule.issuer(), rule);
    }
  }

  JwksData* findByIssuer(const std::string& name) override {
    auto it = jwks_data_map_.find(name);
    if (it == jwks_data_map_.end()) {
      return nullptr;
    }
    return &it->second;
  }

private:
  // The Jwks data map indexed by issuer.
  std::unordered_map<std::string, JwksDataImpl> jwks_data_map_;
};

} // namespace

JwksCachePtr JwksCache::create(
    const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config) {
  return JwksCachePtr(new JwksCacheImpl(config));
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
