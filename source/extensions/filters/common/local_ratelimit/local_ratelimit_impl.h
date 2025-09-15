#pragma once

#include <chrono>
#include <ratio>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/singleton/instance.h"
#include "envoy/type/v3/token_bucket.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/thread_synchronizer.h"
#include "source/common/common/token_bucket_impl.h"
#include "source/common/config/subscription_base.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

class LocalRateLimiterImpl;
class RateLimitTokenBucket;
using RateLimitTokenBucketSharedPtr = std::shared_ptr<RateLimitTokenBucket>;
using ProtoLocalClusterRateLimit = envoy::extensions::common::ratelimit::v3::LocalClusterRateLimit;

class DynamicDescriptor : public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  DynamicDescriptor(uint64_t max_tokens, uint64_t tokens_per_fill,
                    std::chrono::milliseconds fill_interval, uint32_t lru_size, TimeSource&);
  // add a new user configured descriptor to the set.
  RateLimitTokenBucketSharedPtr addOrGetDescriptor(const RateLimit::Descriptor& request_descriptor);

private:
  using LruList = std::list<RateLimit::Descriptor>;

  mutable absl::Mutex dyn_desc_lock_;
  RateLimit::Descriptor::Map<std::pair<RateLimitTokenBucketSharedPtr, LruList::iterator>>
      dynamic_descriptors_ ABSL_GUARDED_BY(dyn_desc_lock_);

  uint64_t max_tokens_;
  uint64_t tokens_per_fill_;
  const std::chrono::milliseconds fill_interval_;
  LruList lru_list_;
  uint32_t lru_size_;
  TimeSource& time_source_;
};

using DynamicDescriptorSharedPtr = std::shared_ptr<DynamicDescriptor>;

class DynamicDescriptorMap : public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  // add a new user configured descriptor to the set.
  void addDescriptor(const RateLimit::LocalDescriptor& descriptor,
                     DynamicDescriptorSharedPtr dynamic_descriptor);
  // pass request_descriptors to the dynamic descriptor set to get the token bucket.
  RateLimitTokenBucketSharedPtr getBucket(const RateLimit::Descriptor);

private:
  bool matchDescriptorEntries(const std::vector<RateLimit::DescriptorEntry>& request_entries,
                              const std::vector<RateLimit::DescriptorEntry>& user_entries);
  RateLimit::LocalDescriptor::Map<DynamicDescriptorSharedPtr> config_descriptors_;
};

class ShareProvider {
public:
  virtual ~ShareProvider() = default;
  // The share of the tokens. This method should be thread-safe.
  virtual double getTokensShareFactor() const PURE;
};
using ShareProviderSharedPtr = std::shared_ptr<ShareProvider>;

class ShareProviderManager;
using ShareProviderManagerSharedPtr = std::shared_ptr<ShareProviderManager>;

class ShareProviderManager : public Singleton::Instance {
public:
  ShareProviderSharedPtr getShareProvider(const ProtoLocalClusterRateLimit& config) const;
  ~ShareProviderManager() override;

  static ShareProviderManagerSharedPtr singleton(Event::Dispatcher& dispatcher,
                                                 Upstream::ClusterManager& cm,
                                                 Singleton::Manager& manager);

  class ShareMonitor : public ShareProvider {
  public:
    virtual double onLocalClusterUpdate(const Upstream::Cluster& cluster) PURE;
  };
  using ShareMonitorSharedPtr = std::shared_ptr<ShareMonitor>;

private:
  ShareProviderManager(Event::Dispatcher& main_dispatcher, const Upstream::Cluster& cluster);

  Event::Dispatcher& main_dispatcher_;
  const Upstream::Cluster& cluster_;
  Envoy::Common::CallbackHandlePtr handle_;
  ShareMonitorSharedPtr share_monitor_;
};
using ShareProviderManagerSharedPtr = std::shared_ptr<ShareProviderManager>;

class TokenBucketContext {
public:
  virtual ~TokenBucketContext() = default;

  virtual uint64_t maxTokens() const PURE;
  virtual uint64_t remainingTokens() const PURE;
  virtual uint64_t resetSeconds() const PURE;
};

class RateLimitTokenBucket : public TokenBucketContext,
                             public Logger::Loggable<Logger::Id::local_rate_limit> {
public:
  RateLimitTokenBucket(uint64_t max_tokens, uint64_t tokens_per_fill,
                       std::chrono::milliseconds fill_interval, TimeSource& time_source);

  // RateLimitTokenBucket
  bool consume(double factor = 1.0, uint64_t tokens = 1);
  double fillRate() const { return token_bucket_.fillRate(); }
  std::chrono::milliseconds fillInterval() const { return fill_interval_; }

  uint64_t maxTokens() const override { return static_cast<uint64_t>(token_bucket_.maxTokens()); }
  uint64_t remainingTokens() const override {
    return static_cast<uint64_t>(token_bucket_.remainingTokens());
  }
  uint64_t resetSeconds() const override {
    return static_cast<uint64_t>(std::ceil(token_bucket_.nextTokenAvailable().count() / 1000));
  }

private:
  AtomicTokenBucketImpl token_bucket_;
  const std::chrono::milliseconds fill_interval_;
};
using RateLimitTokenBucketSharedPtr = std::shared_ptr<RateLimitTokenBucket>;

class LocalRateLimiterImpl : public Logger::Loggable<Logger::Id::local_rate_limit> {
public:
  struct Result {
    bool allowed{};
    std::shared_ptr<const TokenBucketContext> token_bucket_context{};
  };

  LocalRateLimiterImpl(
      const std::chrono::milliseconds fill_interval, const uint64_t max_tokens,
      const uint64_t tokens_per_fill, Event::Dispatcher& dispatcher,
      const Protobuf::RepeatedPtrField<
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors,
      bool always_consume_default_token_bucket = true,
      ShareProviderSharedPtr shared_provider = nullptr, const uint32_t lru_size = 20);
  ~LocalRateLimiterImpl();

  Result requestAllowed(absl::Span<const RateLimit::Descriptor> request_descriptors);

private:
  RateLimitTokenBucketSharedPtr default_token_bucket_;

  TimeSource& time_source_;
  RateLimit::LocalDescriptor::Map<RateLimitTokenBucketSharedPtr> descriptors_;

  DynamicDescriptorMap dynamic_descriptors_{};
  ShareProviderSharedPtr share_provider_;

  mutable Thread::ThreadSynchronizer synchronizer_; // Used for testing only.
  const bool always_consume_default_token_bucket_{};
};

class RateLimiterProviderSingleton;
using RateLimiterProviderSingletonSharedPtr = std::shared_ptr<RateLimiterProviderSingleton>;
class RateLimiterProviderSingleton : public Singleton::Instance {
public:
  class RateLimiterWrapper {
  public:
    RateLimiterWrapper(RateLimiterProviderSingletonSharedPtr provider, std::string key,
                       std::shared_ptr<LocalRateLimiterImpl> limiter,
                       size_t token_bucket_config_hash)
        : provider_(std::move(provider)), name_(std::move(key)), limiter_(std::move(limiter)),
          token_bucket_config_hash_(token_bucket_config_hash) {}

    const std::string& getKey() const { return name_; }
    std::shared_ptr<LocalRateLimiterImpl> getLimiter() const { return limiter_; }
    void setLimiter(std::shared_ptr<LocalRateLimiterImpl> limiter) {
      limiter_ = std::move(limiter);
    }
    size_t getTokenBucketConfigHash() const { return token_bucket_config_hash_; }

  private:
    // The `map_` holds the ownership of this singleton by shared
    // pointer, as the rate limiter map singleton isn't pinned and is
    // shared among all the access log rate limit filters.
    RateLimiterProviderSingletonSharedPtr provider_;

    // The name for `limiter_` in `provider_->limiters_`.
    std::string name_;

    // The `limiter_` holds the ownership of the rate limiter(with the
    // underlying token bucket) by shared pointer. Access loggers using the same
    // `resource_name` of token bucket will share the same rate limiter.
    std::shared_ptr<LocalRateLimiterImpl> limiter_;

    // The hash of the token bucket config used by the rate limiter.
    size_t token_bucket_config_hash_;
  };
  using RateLimiterPtr = std::unique_ptr<RateLimiterWrapper>;

  using SetRateLimiterCb = std::function<void(std::shared_ptr<LocalRateLimiterImpl>)>;
  static RateLimiterPtr getRateLimiter(Server::Configuration::FactoryContext& factory_context,
                                       absl::string_view key,
                                       const envoy::config::core::v3::ConfigSource& config_source,
                                       SetRateLimiterCb callback);

  RateLimiterProviderSingleton(Server::Configuration::ServerFactoryContext& factory_context,
                               const envoy::config::core::v3::ConfigSource& config_source)
      : factory_context_(factory_context), config_source_(config_source),
        scope_(factory_context.scope().createScope("local_ratelimit")), subscription_(*this) {
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

  struct RateLimiterRef {
    envoy::type::v3::TokenBucketConfig config_;
    std::size_t token_bucket_config_hash_;
    std::weak_ptr<LocalRateLimiterImpl> limiter_;
  };

  Server::Configuration::ServerFactoryContext& factory_context_;
  const envoy::config::core::v3::ConfigSource config_source_;
  Stats::ScopeSharedPtr scope_;
  absl::flat_hash_map<std::string, RateLimiterRef> limiters_;
  absl::flat_hash_map<std::string, std::vector<SetRateLimiterCb>> setter_cbs_;
  RatelimiterSubscription subscription_;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
