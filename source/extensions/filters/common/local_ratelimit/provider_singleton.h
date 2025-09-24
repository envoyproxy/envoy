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
        scope_(factory_context.scope().createScope("local_ratelimit_discovery")) {}

  class TokenBucketSubscription : Config::SubscriptionBase<envoy::type::v3::TokenBucketConfig> {
  public:
    explicit TokenBucketSubscription(RateLimiterProviderSingleton& parent,
                                     absl::string_view resource_name);

    void start() { subscription_->start({}); }

    ~TokenBucketSubscription() override = default;

    void addSetter(SetRateLimiterCb callback) { setters_.push_back(std::move(callback)); }

    std::shared_ptr<LocalRateLimiterImpl> getLimiter();

  private:
    void handleAddedResource(const Config::DecodedResourceRef& resource);
    void handleRemovedResource(absl::string_view resource);

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
    std::unique_ptr<Init::TargetImpl> init_target_;
    std::string resource_name_;
    Config::SubscriptionPtr subscription_;
    std::vector<SetRateLimiterCb> setters_;
    absl::optional<envoy::type::v3::TokenBucketConfig> config_;
    std::weak_ptr<LocalRateLimiterImpl> limiter_;
    size_t token_bucket_config_hash_;
  };

  Server::Configuration::ServerFactoryContext& factory_context_;
  const envoy::config::core::v3::ConfigSource config_source_;
  Stats::ScopeSharedPtr scope_;
  absl::flat_hash_map<std::string, std::unique_ptr<TokenBucketSubscription>> subscriptions_;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
