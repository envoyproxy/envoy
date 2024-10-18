#pragma once

#include <chrono>
#include <ratio>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/singleton/instance.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/thread_synchronizer.h"
#include "source/common/common/token_bucket_impl.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

using ProtoLocalClusterRateLimit = envoy::extensions::common::ratelimit::v3::LocalClusterRateLimit;

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

  virtual uint32_t maxTokens() const PURE;
  virtual uint32_t remainingTokens() const PURE;
  virtual absl::optional<int64_t> remainingFillInterval() const PURE;
};

class RateLimitTokenBucket : public TokenBucketContext {
public:
  virtual bool consume(double factor = 1.0) PURE;
  virtual bool isIdle() PURE;
  virtual void onFillTimer(uint64_t refill_counter, double factor = 1.0) PURE;
  virtual std::chrono::milliseconds fillInterval() const PURE;
  virtual double fillRate() const PURE;
  virtual uint64_t multiplier() const { return 1; }
};
using RateLimitTokenBucketSharedPtr = std::shared_ptr<RateLimitTokenBucket>;

class LocalRateLimiterImpl;

class ThreadLocalDynamicDescriptorsCache : public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalDynamicDescriptorsCache(
      RateLimit::LocalDescriptor::Map<RateLimitTokenBucketSharedPtr> cache)
      : dynamic_descriptors_(cache) {}

  const RateLimit::LocalDescriptor::Map<RateLimitTokenBucketSharedPtr> cache() const {
    return dynamic_descriptors_;
  };

private:
  RateLimit::LocalDescriptor::Map<RateLimitTokenBucketSharedPtr> dynamic_descriptors_;
};
using ThreadLocalDynamicDescriptorsCacheSharedPtr =
    std::shared_ptr<ThreadLocalDynamicDescriptorsCache>;

// Token bucket that implements based on the periodic timer.
class TimerTokenBucket : public RateLimitTokenBucket {
public:
  TimerTokenBucket(uint32_t max_tokens, uint32_t tokens_per_fill,
                   std::chrono::milliseconds fill_interval, uint64_t multiplier,
                   LocalRateLimiterImpl& parent);

  // RateLimitTokenBucket
  bool consume(double factor) override;
  bool isIdle() override { return false; /* TODO: implement*/ }
  void onFillTimer(uint64_t refill_counter, double factor) override;
  std::chrono::milliseconds fillInterval() const override { return fill_interval_; }
  double fillRate() const override { return fill_rate_; }
  uint64_t multiplier() const override { return multiplier_; }
  uint32_t maxTokens() const override { return max_tokens_; }
  uint32_t remainingTokens() const override { return tokens_.load(); }
  absl::optional<int64_t> remainingFillInterval() const override;

  // Descriptor refill interval is a multiple of the timer refill interval.
  // For example, if the descriptor refill interval is 150ms and the global
  // refill interval is 50ms, the value is 3. Every 3rd invocation of
  // the global timer, the descriptor is refilled.
  const uint64_t multiplier_{};
  LocalRateLimiterImpl& parent_;
  std::atomic<uint32_t> tokens_{};
  std::atomic<MonotonicTime> fill_time_{};

  const uint32_t max_tokens_{};
  const uint32_t tokens_per_fill_{};
  const std::chrono::milliseconds fill_interval_{};
  const double fill_rate_{};
};

class AtomicTokenBucket : public RateLimitTokenBucket,
                          public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  AtomicTokenBucket(uint32_t max_tokens, uint32_t tokens_per_fill,
                    std::chrono::milliseconds fill_interval, TimeSource& time_source);

  // RateLimitTokenBucket
  bool consume(double factor) override;
  bool isIdle() override;
  void onFillTimer(uint64_t, double) override {}
  std::chrono::milliseconds fillInterval() const override { return fill_interval_; }
  double fillRate() const override { return token_bucket_.fillRate(); }
  uint32_t maxTokens() const override { return static_cast<uint32_t>(token_bucket_.maxTokens()); }
  uint32_t remainingTokens() const override {
    return static_cast<uint32_t>(token_bucket_.remainingTokens());
  }
  absl::optional<int64_t> remainingFillInterval() const override { return {}; }

private:
  AtomicTokenBucketImpl token_bucket_;
  const std::chrono::milliseconds fill_interval_;
};

class LocalRateLimiterImpl : public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  struct Result {
    bool allowed{};
    OptRef<const TokenBucketContext> token_bucket_context{};
  };

  LocalRateLimiterImpl(
      const std::chrono::milliseconds fill_interval, const uint32_t max_tokens,
      const uint32_t tokens_per_fill, Event::Dispatcher& dispatcher,
      ThreadLocal::SlotAllocator& tls,
      const Protobuf::RepeatedPtrField<
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors,
      bool always_consume_default_token_bucket = true,
      ShareProviderSharedPtr shared_provider = nullptr, const uint32_t lru_size = 20);
  ~LocalRateLimiterImpl();

  Result requestAllowed(absl::Span<const RateLimit::LocalDescriptor> request_descriptors);

private:
  void onFillTimer();
  bool compareDescriptorEntries(const std::vector<RateLimit::DescriptorEntry>& request_entries,
                                const std::vector<RateLimit::DescriptorEntry>& user_entries,
                                std::vector<RateLimit::DescriptorEntry>& new_descriptor_entries);
  void notifyMainThread(RateLimitTokenBucketSharedPtr token_bucket,
                        RateLimit::LocalDescriptor new_descriptor);
  const ThreadLocalDynamicDescriptorsCache& threadLocal() const {
    return tls_->getTyped<ThreadLocalDynamicDescriptorsCache>();
  }

  RateLimitTokenBucketSharedPtr default_token_bucket_;

  const Event::TimerPtr fill_timer_;
  TimeSource& time_source_;
  RateLimit::LocalDescriptor::Map<RateLimitTokenBucketSharedPtr> descriptors_;

  RateLimit::LocalDescriptor::Map<RateLimitTokenBucketSharedPtr> dynamic_descriptors_;
  ThreadLocal::SlotPtr tls_;

  // Refill counter is incremented per each refill timer hit.
  uint64_t refill_counter_{0};

  ShareProviderSharedPtr share_provider_;

  mutable Thread::ThreadSynchronizer synchronizer_; // Used for testing only.
  const bool always_consume_default_token_bucket_{};
  const bool no_timer_based_rate_limit_token_bucket_{};
  Event::Dispatcher& dispatcher_;

  using LruList = std::list<RateLimit::LocalDescriptor>;
  LruList lru_list_;
  uint32_t lru_size_;

  friend class LocalRateLimiterImplTest;
  friend class TimerTokenBucket;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
