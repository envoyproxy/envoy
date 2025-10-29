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

// RateLimiterProviderSingleton and its child classes are used to achieve the
// rate limiter singletons shared within the process.
//
// High-level architecture:
// - RateLimiterProviderSingleton: A process-wide singleton responsible for
//   managing and vending rate limiters. It holds subscriptions to token bucket
//   configurations.
// - TokenBucketSubscription: Manages the subscription to a specific token
//   bucket resource via xDS. It receives configuration updates and creates or
//   updates the underlying LocalRateLimiterImpl.
// - LocalRateLimiterImpl: The actual rate limiter implementation based on the
//   token bucket algorithm. Instances are shared among filters requesting the
//   same resource.
// - RateLimiterWrapper: A wrapper class holding shared pointers to the
//   provider, subscription, and limiter. This ensures that the necessary
//   components remain alive as long as they are in use by any filter.
//
// Workflow:
// 1. A filter requests a rate limiter for a specific key (resource name).
// 2. RateLimiterProviderSingleton::getRateLimiter is called.
// 3. It looks up or creates a TokenBucketSubscription for the given resource
//    name.
// 4. The TokenBucketSubscription establishes an xDS subscription to fetch the
//    envoy::type::v3::TokenBucket.
// 5. Upon receiving the configuration, the TokenBucketSubscription creates or
//    updates a shared LocalRateLimiterImpl instance.
// 6. The getRateLimiter method returns a RateLimiterWrapper, which provides
//    access to the shared LocalRateLimiterImpl. The wrapper's shared pointers
//    keep the subscription and provider alive.
// 7. When the configuration is updated via xDS, the TokenBucketSubscription
//    updates the shared LocalRateLimiterImpl instance, affecting all filters
//    using it.
// 8. When the configuration is removed via xDS, the TokenBucketSubscription
//    resets the shared LocalRateLimiterImpl instance to an AlwaysDeny limiter.
//    This prevents new filters from using the old limiter.
class RateLimiterProviderSingleton;
using RateLimiterProviderSingletonSharedPtr = std::shared_ptr<RateLimiterProviderSingleton>;
class RateLimiterProviderSingleton : public Singleton::Instance {
public:
  class TokenBucketSubscription;
  using TokenBucketSubscriptionSharedPtr = std::shared_ptr<TokenBucketSubscription>;
  struct ThreadLocalLimiter : public Envoy::ThreadLocal::ThreadLocalObject {
    ThreadLocalLimiter(LocalRateLimiterSharedPtr limiter) : limiter(limiter) {}
    LocalRateLimiterSharedPtr limiter = nullptr;
  };

  class RateLimiterWrapper {
  public:
    RateLimiterWrapper(ThreadLocal::Instance& tls, RateLimiterProviderSingletonSharedPtr provider,
                       TokenBucketSubscriptionSharedPtr subscription,
                       std::shared_ptr<LocalRateLimiterImpl> limiter,
                       std::unique_ptr<Init::TargetImpl> init_target)
        : cancelled_(std::make_shared<std::atomic<bool>>(false)), provider_(provider),
          subscription_(subscription), limiter_slot_(tls), init_target_(std::move(init_target)) {
      limiter_slot_.set([l = limiter](Envoy::Event::Dispatcher&) {
        return std::make_shared<ThreadLocalLimiter>(l);
      });
    }

    LocalRateLimiterSharedPtr getLimiter() const { return limiter_slot_.get()->limiter; }

    void setLimiter(LocalRateLimiterSharedPtr limiter);

    ~RateLimiterWrapper() { cancelled_->store(true); }

    TokenBucketSubscriptionSharedPtr getSubscription() const { return subscription_; }

  private:
    // The bool to denote if the object is deleted so we need to cancel the async setter and the
    // callback.
    std::shared_ptr<std::atomic<bool>> cancelled_;
    // The `provider_` holds the ownership of this singleton by shared
    // pointer, as the rate limiter map singleton isn't pinned and is
    // shared among all the access log rate limit filters.
    RateLimiterProviderSingletonSharedPtr provider_;

    // The `subscription_` holds the ownership of the subscription to the token
    // bucket resource by shared pointer.
    TokenBucketSubscriptionSharedPtr subscription_;

    // The `limiter_slot_` holds the ownership of the rate limiter(with the
    // underlying token bucket) by shared pointer. Access loggers using the same
    // `resource_name` of token bucket will share the same rate limiter.
    //
    // The `limiter_slot_` is thread-safe and can be accessed by multiple
    // threads. It protects the `limiter` from being read and updated
    // concurrently when listeners are active and there are new TokenBucket
    // resources coming.
    Envoy::ThreadLocal::TypedSlot<ThreadLocalLimiter> limiter_slot_;

    // The `init_target_` is used to wait for the rate limiter to be set. It
    // makes sure the access logger won't log until the rate limiter is ready.
    std::unique_ptr<Init::TargetImpl> init_target_;
  };
  using RateLimiterWrapperPtr = std::unique_ptr<RateLimiterWrapper>;

  using SetRateLimiterCb = std::function<void(LocalRateLimiterSharedPtr)>;
  static RateLimiterWrapperPtr
  getRateLimiter(Server::Configuration::ServerFactoryContext& factory_context,
                 absl::string_view key, const envoy::config::core::v3::ConfigSource& config_source,
                 intptr_t setter_key, SetRateLimiterCb setter);

  RateLimiterProviderSingleton(Server::Configuration::ServerFactoryContext& factory_context,
                               const envoy::config::core::v3::ConfigSource& config_source)
      : factory_context_(factory_context), config_source_(config_source),
        scope_(factory_context.scope().createScope("local_ratelimit_discovery")),
        fallback_always_deny_limiter_(std::make_shared<AlwaysDenyLocalRateLimiter>()) {}

  class TokenBucketSubscription : Config::SubscriptionBase<envoy::type::v3::TokenBucket> {
  public:
    explicit TokenBucketSubscription(RateLimiterProviderSingleton& parent,
                                     absl::string_view resource_name);

    ~TokenBucketSubscription() override;

    void addSetter(intptr_t key, SetRateLimiterCb callback) { setters_[key] = std::move(callback); }

    void removeSetter(intptr_t key) { setters_.erase(key); }

    std::shared_ptr<LocalRateLimiterImpl> getLimiter();

    // Config::SubscriptionCallbacks
    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                const std::string&) override;

    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                const std::string&) override;

    void onConfigUpdateFailed(Config::ConfigUpdateFailureReason, const EnvoyException*) override {}

  private:
    void handleAddedResource(const Config::DecodedResourceRef& resource);
    void handleRemovedResource(absl::string_view resource_name);

    RateLimiterProviderSingleton& parent_;
    std::string resource_name_;
    Config::SubscriptionPtr subscription_;
    absl::flat_hash_map<intptr_t, SetRateLimiterCb> setters_;
    absl::optional<envoy::type::v3::TokenBucket> config_;
    std::weak_ptr<LocalRateLimiterImpl> limiter_;
    size_t token_bucket_config_hash_;
  };

  Server::Configuration::ServerFactoryContext& factory_context_;
  const envoy::config::core::v3::ConfigSource config_source_;
  Stats::ScopeSharedPtr scope_;
  LocalRateLimiterSharedPtr fallback_always_deny_limiter_;
  absl::flat_hash_map<std::string, std::weak_ptr<TokenBucketSubscription>> subscriptions_;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
