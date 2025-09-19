#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/type/v3/token_bucket.pb.h"
#include "envoy/type/v3/token_bucket.pb.validate.h"

#include "source/common/config/subscription_base.h"
#include "source/common/init/target_impl.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

class RateLimiterProviderSingleton;
using RateLimiterProviderSingletonSharedPtr = std::shared_ptr<RateLimiterProviderSingleton>;
class RateLimiterProviderSingleton : public Singleton::Instance {
public:
  class RateLimiterWrapper {
  public:
    RateLimiterWrapper(RateLimiterProviderSingletonSharedPtr provider,
                       std::shared_ptr<LocalRateLimiterImpl> limiter)
        : provider_(provider), limiter_(limiter) {}
    std::shared_ptr<LocalRateLimiterImpl> getLimiter() const { return limiter_; }
    void setLimiter(std::shared_ptr<LocalRateLimiterImpl> limiter) { limiter_ = limiter; }

  private:
    // The `provider_` holds the ownership of this singleton by shared
    // pointer, as the rate limiter map singleton isn't pinned and is
    // shared among all the access log rate limit filters.
    RateLimiterProviderSingletonSharedPtr provider_;

    // The `limiter_` holds the ownership of the rate limiter(with the
    // underlying token bucket) by shared pointer. Access loggers using the same
    // `resource_name` of token bucket will share the same rate limiter.
    std::shared_ptr<LocalRateLimiterImpl> limiter_;
  };
  using RateLimiterWrapperPtr = std::unique_ptr<RateLimiterWrapper>;

  using SetRateLimiterCb = std::function<void(std::shared_ptr<LocalRateLimiterImpl>)>;
  static RateLimiterWrapperPtr
  getRateLimiter(Server::Configuration::ServerFactoryContext& factory_context,
                 absl::string_view key, const envoy::config::core::v3::ConfigSource& config_source,
                 SetRateLimiterCb callback);

  RateLimiterProviderSingleton(Server::Configuration::ServerFactoryContext& factory_context,
                               const envoy::config::core::v3::ConfigSource& config_source)
      : factory_context_(factory_context), config_source_(config_source),
        scope_(factory_context.scope().createScope("local_ratelimit_discovery")),
        subscription_(*this) {
    subscription_.start();
  }

  class RatelimiterSubscription : Config::SubscriptionBase<envoy::type::v3::TokenBucketConfig> {
  public:
    explicit RatelimiterSubscription(RateLimiterProviderSingleton& parent);

    void start() { subscription_->start({}); }

    ~RatelimiterSubscription() override = default;

  private:
    // Handles the addition or update of rate limiters based on new resources.
    // The logic is
    // 1. If the new token bucket config name is found in the `limiters_` and
    // the config is different, update the config in the `limiters_` and
    // reset the `limiter_` for future usage.
    // 2. If the new token bucket config name has setters callbacks,
    //    - if there is a alive limiter, return that limiter.
    //    - if there is no alive limiter, create a new limiter and return it
    //    to the callbacks.
    void handleAddedResources(const std::vector<Config::DecodedResourceRef>& added_resources);

    // Handles the removal of rate limiters based on removed resources.
    // The logic is
    // 1. If the removed token bucket config name is found in the `limiters_`
    // and there is alive rate limiter, remove the rate limiter from the
    // `limiters_` for future usage. Please note the limiters are held by
    // access log and they will be destroyed when the access log is destroyed.
    // 2. If the removed token bucket config name is not found in the
    // `limiters_`, no op.
    void handleRemovedResources(const Protobuf::RepeatedPtrField<std::string>& removed_resources);

    // Config::SubscriptionCallbacks
    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                const std::string&) override;

    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                const std::string&) override;

    void onConfigUpdateFailed(Config::ConfigUpdateFailureReason, const EnvoyException*) override {
      // Do nothing - feature is automatically disabled.
    }

    RateLimiterProviderSingleton& parent_;
    Config::SubscriptionPtr subscription_;
  };

  class RateLimiterRef {
  public:
    RateLimiterRef(const envoy::type::v3::TokenBucketConfig& config,
                   std::weak_ptr<LocalRateLimiterImpl> limiter)
        : config_(config), limiter_(limiter), token_bucket_config_hash_(MessageUtil::hash(config)) {
    }

    const envoy::type::v3::TokenBucketConfig& config() const { return config_; }
    std::weak_ptr<LocalRateLimiterImpl> limiter() const { return limiter_; }

    void setConfig(const envoy::type::v3::TokenBucketConfig& config) {
      size_t new_hash = MessageUtil::hash(config);
      if (new_hash != token_bucket_config_hash_) {
        config_ = config;
        token_bucket_config_hash_ = new_hash;
        limiter_.reset();
      }
    }
    void setLimiter(std::weak_ptr<LocalRateLimiterImpl> limiter) { limiter_ = limiter; }

  private:
    envoy::type::v3::TokenBucketConfig config_;
    std::weak_ptr<LocalRateLimiterImpl> limiter_;
    size_t token_bucket_config_hash_;
  };

  class RateLimitConfigCallback {
  public:
    explicit RateLimitConfigCallback(Server::Configuration::ServerFactoryContext& factory_context);

    void addCallback(SetRateLimiterCb callback);

    void setLimiter(std::shared_ptr<LocalRateLimiterImpl> limiter);

  private:
    // The init target to wait for the rate limiter config to be ready.
    std::unique_ptr<Init::TargetImpl> init_target_;

    // A list of callbacks to set the rate limiter in the access log rate
    // limiter filter.
    std::vector<SetRateLimiterCb> setters_;
  };

  Server::Configuration::ServerFactoryContext& factory_context_;
  const envoy::config::core::v3::ConfigSource config_source_;
  Stats::ScopeSharedPtr scope_;
  absl::flat_hash_map<std::string, RateLimiterRef> limiters_;
  absl::flat_hash_map<std::string, RateLimitConfigCallback> setter_cbs_;
  RatelimiterSubscription subscription_;
};
} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
