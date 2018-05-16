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
const int kPubkeyCacheExpirationSec = 600;

// HTTP Protocol scheme prefix in JWT aud claim.
const std::string kHTTPSchemePrefix("http://");

// HTTPS Protocol scheme prefix in JWT aud claim.
const std::string kHTTPSSchemePrefix("https://");

/**
 * Searches protocol scheme prefix and trailing slash from an audience string.
 * returns one without these prefix and suffix for consistent comparison.
 * @param input audience string.
 * @return sanitized audience string without scheme prefix and tailing slash.
 */
std::string sanitizeAudience(const std::string& aud) {
  int beg = 0;
  int end = aud.length() - 1;
  bool sanitize_aud = false;
  // Point beg to first character after protocol scheme prefix in audience.
  if (aud.compare(0, kHTTPSchemePrefix.length(), kHTTPSchemePrefix) == 0) {
    beg = kHTTPSchemePrefix.length();
    sanitize_aud = true;
  } else if (aud.compare(0, kHTTPSSchemePrefix.length(), kHTTPSSchemePrefix) == 0) {
    beg = kHTTPSSchemePrefix.length();
    sanitize_aud = true;
  }
  // Point end to trailing slash in aud.
  if (end >= 0 && aud[end] == '/') {
    --end;
    sanitize_aud = true;
  }
  if (sanitize_aud) {
    return aud.substr(beg, end - beg + 1);
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
      expire += std::chrono::seconds(kPubkeyCacheExpirationSec);
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
