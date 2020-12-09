#include "extensions/filters/http/jwt_authn/jwks_cache.h"

#include <chrono>
#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "common/common/logger.h"
#include "common/config/datasource.h"
#include "common/protobuf/utility.h"

#include "absl/container/node_hash_map.h"
#include "jwt_verify_lib/check_audience.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using envoy::extensions::filters::http::jwt_authn::v3::JwtProvider;
using ::google::jwt_verify::Jwks;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// Default cache expiration time in 5 minutes.
constexpr int PubkeyCacheExpirationSec = 600;

class JwksDataImpl : public Cache::JwksData, public Logger::Loggable<Logger::Id::jwt> {
public:
  JwksDataImpl(const JwtProvider& jwt_provider, TimeSource& time_source, Api::Api& api)
      : jwt_provider_(jwt_provider), time_source_(time_source) {
    std::vector<std::string> audiences;
    for (const auto& aud : jwt_provider_.audiences()) {
      audiences.push_back(aud);
    }
    audiences_ = std::make_unique<::google::jwt_verify::CheckAudience>(audiences);

    const auto inline_jwks = Config::DataSource::read(jwt_provider_.local_jwks(), true, api);
    if (!inline_jwks.empty()) {
      auto ptr = setKey(
          ::google::jwt_verify::Jwks::createFrom(inline_jwks, ::google::jwt_verify::Jwks::JWKS),
          std::chrono::steady_clock::time_point::max());
      if (ptr->getStatus() != Status::Ok) {
        ENVOY_LOG(warn, "Invalid inline jwks for issuer: {}, jwks: {}", jwt_provider_.issuer(),
                  inline_jwks);
        jwks_obj_.reset(nullptr);
      }
    }
  }

  const JwtProvider& getJwtProvider() const override { return jwt_provider_; }

  bool areAudiencesAllowed(const std::vector<std::string>& jwt_audiences) const override {
    return audiences_->areAudiencesAllowed(jwt_audiences);
  }

  const Jwks* getJwksObj() const override { return jwks_obj_.get(); }

  bool isExpired() const override { return time_source_.monotonicTime() >= expiration_time_; }

  const ::google::jwt_verify::Jwks* setRemoteJwks(::google::jwt_verify::JwksPtr&& jwks) override {
    return setKey(std::move(jwks), getRemoteJwksExpirationTime());
  }

  Status getJwtStatus() override { return jwt_status_; }

  void addTokenResult(const std::string& token, ::google::jwt_verify::Jwt& token_result) override {
    jwt_status_ = token_result.parseFromString(token);
    if (jwt_status_ == Status::Ok) {
      jwt_ = std::make_unique<::google::jwt_verify::Jwt>(token_result);
    }
  }

  bool findTokenResult(const std::string& token, ::google::jwt_verify::Jwt& token_result) override {
    Status status = token_result.parseFromString(token);
    if (status != Status::Ok) {
      return false;
    }
    return true;
  }

private:
  // Get the expiration time for a remote Jwks
  std::chrono::steady_clock::time_point getRemoteJwksExpirationTime() const {
    auto expire = time_source_.monotonicTime();
    if (jwt_provider_.has_remote_jwks() && jwt_provider_.remote_jwks().has_cache_duration()) {
      expire += std::chrono::milliseconds(
          DurationUtil::durationToMilliseconds(jwt_provider_.remote_jwks().cache_duration()));
    } else {
      expire += std::chrono::seconds(PubkeyCacheExpirationSec);
    }
    return expire;
  }

  const ::google::jwt_verify::Jwks* setKey(::google::jwt_verify::JwksPtr&& jwks,
                                           MonotonicTime expire) {
    jwks_obj_ = std::move(jwks);
    expiration_time_ = expire;
    return jwks_obj_.get();
  }

  // The jwt provider config.
  const JwtProvider& jwt_provider_;
  // Check audience object
  ::google::jwt_verify::CheckAudiencePtr audiences_;
  // The generated jwks object.
  ::google::jwt_verify::JwksPtr jwks_obj_;
  TimeSource& time_source_;
  // The pubkey expiration time.
  MonotonicTime expiration_time_;
  // Jwt object for verified token.
  std::unique_ptr<::google::jwt_verify::Jwt> jwt_;
  // A valid error on parsing Jwt.
  Status jwt_status_;
};

class JwksCacheImpl : public Cache {
public:
  // Load the config from envoy config.
  JwksCacheImpl(const JwtAuthentication& config, TimeSource& time_source, Api::Api& api) {
    for (const auto& it : config.providers()) {
      const auto& provider = it.second;
      jwks_data_map_.emplace(it.first, JwksDataImpl(provider, time_source, api));
      if (issuer_ptr_map_.find(provider.issuer()) == issuer_ptr_map_.end()) {
        issuer_ptr_map_.emplace(provider.issuer(), findByProvider(it.first));
      }
    }
  }

  JwksData* findByIssuer(const std::string& issuer) override {
    const auto it = issuer_ptr_map_.find(issuer);
    if (it == issuer_ptr_map_.end()) {
      return nullptr;
    }
    return it->second;
  }

  JwksData* findByProvider(const std::string& provider) final {
    const auto it = jwks_data_map_.find(provider);
    if (it == jwks_data_map_.end()) {
      return nullptr;
    }
    return &it->second;
  }

private:
  // The Jwks data map indexed by provider.
  absl::node_hash_map<std::string, JwksDataImpl> jwks_data_map_;
  // The Jwks data pointer map indexed by issuer.
  absl::node_hash_map<std::string, JwksData*> issuer_ptr_map_;
};

} // namespace

CachePtr
Cache::create(const envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication& config,
              TimeSource& time_source, Api::Api& api) {
  return CachePtr(new JwksCacheImpl(config, time_source, api));
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
