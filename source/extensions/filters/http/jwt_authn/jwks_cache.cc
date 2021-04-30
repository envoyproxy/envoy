#include "extensions/filters/http/jwt_authn/jwks_cache.h"

#include <chrono>

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

using JwksSharedPtr = std::shared_ptr<::google::jwt_verify::Jwks>;

class JwksDataImpl : public JwksCache::JwksData, public Logger::Loggable<Logger::Id::jwt> {
public:
  JwksDataImpl(const JwtProvider& jwt_provider, TimeSource& time_source, Api::Api& api,
               ThreadLocal::SlotAllocator& tls)
      : jwt_provider_(jwt_provider), time_source_(time_source), tls_(tls) {

    std::vector<std::string> audiences;
    for (const auto& aud : jwt_provider_.audiences()) {
      audiences.push_back(aud);
    }
    audiences_ = std::make_unique<::google::jwt_verify::CheckAudience>(audiences);

    tls_.set([](Envoy::Event::Dispatcher&) { return std::make_shared<ThreadLocalCache>(); });

    const auto inline_jwks = Config::DataSource::read(jwt_provider_.local_jwks(), true, api);
    if (!inline_jwks.empty()) {
      auto jwks =
          ::google::jwt_verify::Jwks::createFrom(inline_jwks, ::google::jwt_verify::Jwks::JWKS);
      if (jwks->getStatus() != Status::Ok) {
        ENVOY_LOG(warn, "Invalid inline jwks for issuer: {}, jwks: {}", jwt_provider_.issuer(),
                  inline_jwks);
      } else {
        setJwksToAllThreads(std::move(jwks), std::chrono::steady_clock::time_point::max());
      }
    }
  }

  const JwtProvider& getJwtProvider() const override { return jwt_provider_; }

  bool areAudiencesAllowed(const std::vector<std::string>& jwt_audiences) const override {
    return audiences_->areAudiencesAllowed(jwt_audiences);
  }

  const Jwks* getJwksObj() const override { return tls_->jwks_.get(); }

  bool isExpired() const override { return time_source_.monotonicTime() >= tls_->expire_; }

  const ::google::jwt_verify::Jwks* setRemoteJwks(::google::jwt_verify::JwksPtr&& jwks) override {
    // convert unique_ptr to shared_ptr
    JwksSharedPtr shared_jwks(jwks.release());
    tls_->jwks_ = shared_jwks;
    tls_->expire_ = getRemoteJwksExpirationTime();
    return shared_jwks.get();
  }

private:
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    // The jwks object.
    JwksSharedPtr jwks_;
    // The pubkey expiration time.
    MonotonicTime expire_;
  };

  // Set jwks shared_ptr to all threads.
  void setJwksToAllThreads(::google::jwt_verify::JwksPtr&& jwks,
                           std::chrono::steady_clock::time_point expire) {
    JwksSharedPtr shared_jwks(jwks.release());
    tls_.runOnAllThreads([shared_jwks, expire](OptRef<ThreadLocalCache> obj) {
      obj->jwks_ = shared_jwks;
      obj->expire_ = expire;
    });
  }

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

  // The jwt provider config.
  const JwtProvider& jwt_provider_;
  // Check audience object
  ::google::jwt_verify::CheckAudiencePtr audiences_;
  // the time source
  TimeSource& time_source_;
  // the thread local slot for cache
  ThreadLocal::TypedSlot<ThreadLocalCache> tls_;
};

using JwksDataImplPtr = std::unique_ptr<JwksDataImpl>;

class JwksCacheImpl : public JwksCache {
public:
  // Load the config from envoy config.
  JwksCacheImpl(const JwtAuthentication& config, TimeSource& time_source, Api::Api& api,
                ThreadLocal::SlotAllocator& tls) {
    for (const auto& it : config.providers()) {
      const auto& provider = it.second;
      auto jwks_data = std::make_unique<JwksDataImpl>(provider, time_source, api, tls);
      if (issuer_ptr_map_.find(provider.issuer()) == issuer_ptr_map_.end()) {
        issuer_ptr_map_.emplace(provider.issuer(), jwks_data.get());
      }
      jwks_data_map_.emplace(it.first, std::move(jwks_data));
    }
  }

  JwksData* findByIssuer(const std::string& issuer) override {
    JwksData* data = findIssuerMap(issuer);
    if (!data && !issuer.empty()) {
      // The first empty issuer from JwtProvider can be used.
      return findIssuerMap(Envoy::EMPTY_STRING);
    }
    return data;
  }

  JwksData* findByProvider(const std::string& provider) override {
    const auto& it = jwks_data_map_.find(provider);
    if (it != jwks_data_map_.end()) {
      return it->second.get();
    }
    // Verifier::innerCreate function makes sure that all provider names are defined.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

private:
  JwksData* findIssuerMap(const std::string& issuer) {
    const auto& it = issuer_ptr_map_.find(issuer);
    if (it == issuer_ptr_map_.end()) {
      return nullptr;
    }
    return it->second;
  }

  // The Jwks data map indexed by provider.
  absl::node_hash_map<std::string, JwksDataImplPtr> jwks_data_map_;
  // The Jwks data pointer map indexed by issuer.
  absl::node_hash_map<std::string, JwksData*> issuer_ptr_map_;
};

} // namespace

JwksCachePtr
JwksCache::create(const envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication& config,
                  TimeSource& time_source, Api::Api& api, ThreadLocal::SlotAllocator& tls) {
  return std::make_unique<JwksCacheImpl>(config, time_source, api, tls);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
