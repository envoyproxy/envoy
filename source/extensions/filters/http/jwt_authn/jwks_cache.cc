#include "source/extensions/filters/http/jwt_authn/jwks_cache.h"

#include <chrono>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/config/datasource.h"
#include "source/common/http/utility.h"

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

class JwksDataImpl : public JwksCache::JwksData, public Logger::Loggable<Logger::Id::jwt> {
public:
  JwksDataImpl(const JwtProvider& jwt_provider, Server::Configuration::FactoryContext& context,
               CreateJwksFetcherCb fetcher_cb, JwtAuthnFilterStats& stats)
      : jwt_provider_(jwt_provider), time_source_(context.timeSource()),
        tls_(context.threadLocal()) {

    // remote_jwks.retry_policy has an invalid case that could not be validated by the
    // proto validation annotation. It has to be validated by the code.
    if (jwt_provider_.has_remote_jwks() && jwt_provider_.remote_jwks().has_retry_policy()) {
      Http::Utility::validateCoreRetryPolicy(jwt_provider_.remote_jwks().retry_policy());
    }

    std::vector<std::string> audiences;
    for (const auto& aud : jwt_provider_.audiences()) {
      audiences.push_back(aud);
    }
    audiences_ = std::make_unique<::google::jwt_verify::CheckAudience>(audiences);
    bool enable_jwt_cache = jwt_provider_.has_jwt_cache_config();
    const auto& config = jwt_provider_.jwt_cache_config();
    tls_.set([enable_jwt_cache, config](Envoy::Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalCache>(enable_jwt_cache, config, dispatcher.timeSource());
    });

    const auto inline_jwks =
        Config::DataSource::read(jwt_provider_.local_jwks(), true, context.api());
    if (!inline_jwks.empty()) {
      auto jwks =
          ::google::jwt_verify::Jwks::createFrom(inline_jwks, ::google::jwt_verify::Jwks::JWKS);
      if (jwks->getStatus() != Status::Ok) {
        ENVOY_LOG(warn, "Invalid inline jwks for issuer: {}, jwks: {}", jwt_provider_.issuer(),
                  inline_jwks);
      } else {
        setJwksToAllThreads(std::move(jwks));
      }
    } else {
      // create async_fetch for remote_jwks, if is no-op if async_fetch is not enabled.
      if (jwt_provider_.has_remote_jwks()) {
        async_fetcher_ = std::make_unique<JwksAsyncFetcher>(
            jwt_provider_.remote_jwks(), context, fetcher_cb, stats,
            [this](google::jwt_verify::JwksPtr&& jwks) { setJwksToAllThreads(std::move(jwks)); });
      }
    }
  }

  const JwtProvider& getJwtProvider() const override { return jwt_provider_; }

  bool areAudiencesAllowed(const std::vector<std::string>& jwt_audiences) const override {
    return audiences_->areAudiencesAllowed(jwt_audiences);
  }

  const Jwks* getJwksObj() const override { return tls_->jwks_.get(); }

  bool isExpired() const override { return time_source_.monotonicTime() >= tls_->expire_; }

  const ::google::jwt_verify::Jwks* setRemoteJwks(JwksConstPtr&& jwks) override {
    // convert unique_ptr to shared_ptr
    JwksConstSharedPtr shared_jwks = std::move(jwks);
    tls_->jwks_ = shared_jwks;
    tls_->expire_ = time_source_.monotonicTime() +
                    JwksAsyncFetcher::getCacheDuration(jwt_provider_.remote_jwks());
    return shared_jwks.get();
  }

  JwtCache& getJwtCache() override { return *tls_->jwt_cache_; }

private:
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache(bool enable_jwt_cache,
                     const envoy::extensions::filters::http::jwt_authn::v3::JwtCacheConfig& config,
                     TimeSource& time_source)
        : jwt_cache_(JwtCache::create(enable_jwt_cache, config, time_source)) {}

    // The jwks object.
    JwksConstSharedPtr jwks_;
    // The JwtCache object
    const JwtCachePtr jwt_cache_;
    // The pubkey expiration time.
    MonotonicTime expire_;
  };

  // Set jwks shared_ptr to all threads.
  void setJwksToAllThreads(JwksConstPtr&& jwks) {
    JwksConstSharedPtr shared_jwks = std::move(jwks);
    tls_.runOnAllThreads([shared_jwks](OptRef<ThreadLocalCache> obj) {
      obj->jwks_ = shared_jwks;
      obj->expire_ = std::chrono::steady_clock::time_point::max();
    });
  }

  // The jwt provider config.
  const JwtProvider& jwt_provider_;
  // Check audience object
  ::google::jwt_verify::CheckAudiencePtr audiences_;
  // the time source
  TimeSource& time_source_;
  // the thread local slot for cache
  ThreadLocal::TypedSlot<ThreadLocalCache> tls_;
  // async fetcher
  JwksAsyncFetcherPtr async_fetcher_;
};

using JwksDataImplPtr = std::unique_ptr<JwksDataImpl>;

class JwksCacheImpl : public JwksCache {
public:
  // Load the config from envoy config.
  JwksCacheImpl(const JwtAuthentication& config, Server::Configuration::FactoryContext& context,
                CreateJwksFetcherCb fetcher_fn, JwtAuthnFilterStats& stats)
      : stats_(stats) {
    for (const auto& [name, provider] : config.providers()) {
      auto jwks_data = std::make_unique<JwksDataImpl>(provider, context, fetcher_fn, stats);
      if (issuer_ptr_map_.find(provider.issuer()) == issuer_ptr_map_.end()) {
        issuer_ptr_map_.emplace(provider.issuer(), jwks_data.get());
      }
      jwks_data_map_.emplace(name, std::move(jwks_data));
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
    PANIC("unexpected");
  }

  JwtAuthnFilterStats& stats() override { return stats_; }

private:
  JwksData* findIssuerMap(const std::string& issuer) {
    const auto& it = issuer_ptr_map_.find(issuer);
    if (it == issuer_ptr_map_.end()) {
      return nullptr;
    }
    return it->second;
  }

  // stats
  JwtAuthnFilterStats& stats_;
  // The Jwks data map indexed by provider.
  absl::node_hash_map<std::string, JwksDataImplPtr> jwks_data_map_;
  // The Jwks data pointer map indexed by issuer.
  absl::node_hash_map<std::string, JwksData*> issuer_ptr_map_;
};

} // namespace

JwksCachePtr
JwksCache::create(const envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication& config,
                  Server::Configuration::FactoryContext& context, CreateJwksFetcherCb fetcher_fn,
                  JwtAuthnFilterStats& stats) {
  return std::make_unique<JwksCacheImpl>(config, context, fetcher_fn, stats);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
