#include "extensions/filters/http/jwt_authn/jwks_cache.h"

#include <chrono>
#include <unordered_map>

#include "common/common/logger.h"
#include "common/config/datasource.h"
#include "common/protobuf/utility.h"

#include "jwt_verify_lib/check_audience.h"

using ::google::jwt_verify::Jwks;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// Default cache expiration time in 5 minutes.
constexpr int PubkeyCacheExpirationSec = 600;

class JwksDataImpl : public JwksCache::JwksData, public Logger::Loggable<Logger::Id::filter> {
public:
  JwksDataImpl(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRule& jwt_rule)
      : jwt_rule_(jwt_rule) {
    std::vector<std::string> audiences;
    for (const auto& aud : jwt_rule_.audiences()) {
      audiences.push_back(aud);
    }
    audiences_ = std::make_unique<::google::jwt_verify::CheckAudience>(audiences);

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
    return audiences_->areAudiencesAllowed(jwt_audiences);
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
      expire += std::chrono::milliseconds(
          DurationUtil::durationToMilliseconds(jwt_rule_.remote_jwks().cache_duration()));
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
  // Check audience object
  ::google::jwt_verify::CheckAudiencePtr audiences_;
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
