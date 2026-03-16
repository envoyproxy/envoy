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
#include "source/extensions/filters/common/local_ratelimit/dynamic_descriptor.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

class LocalRateLimiterImpl;
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

class RateLimitTokenBucket : public TokenBucketContext,
                             public Logger::Loggable<Logger::Id::local_rate_limit> {
public:
  RateLimitTokenBucket(uint64_t max_tokens, uint64_t tokens_per_fill,
                       std::chrono::milliseconds fill_interval, TimeSource& time_source,
                       bool shadow_mode);

  // RateLimitTokenBucket
  bool consume(double factor = 1.0, uint64_t tokens = 1);
  double fillRate() const { return token_bucket_.fillRate(); }
  std::chrono::milliseconds fillInterval() const { return fill_interval_; }

  bool shadowMode() const override { return shadow_mode_; }
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
  const bool shadow_mode_{false};
};
using RateLimitTokenBucketSharedPtr = std::shared_ptr<RateLimitTokenBucket>;

class LocalRateLimiterImpl : public Logger::Loggable<Logger::Id::local_rate_limit>,
                             public LocalRateLimiter {
public:
  LocalRateLimiterImpl(
      const std::chrono::milliseconds fill_interval, const uint64_t max_tokens,
      const uint64_t tokens_per_fill, Event::Dispatcher& dispatcher,
      const Protobuf::RepeatedPtrField<
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors,
      bool always_consume_default_token_bucket = true,
      ShareProviderSharedPtr shared_provider = nullptr, const uint32_t lru_size = 20);
  ~LocalRateLimiterImpl() override;

  LocalRateLimiter::Result
  requestAllowed(absl::Span<const Envoy::RateLimit::Descriptor> request_descriptors) override;

private:
  RateLimitTokenBucketSharedPtr default_token_bucket_;

  TimeSource& time_source_;
  Envoy::RateLimit::LocalDescriptor::Map<RateLimitTokenBucketSharedPtr> descriptors_;

  DynamicDescriptorMap dynamic_descriptors_{};
  ShareProviderSharedPtr share_provider_;

  mutable Thread::ThreadSynchronizer synchronizer_; // Used for testing only.
  const bool always_consume_default_token_bucket_{};
};

class AlwaysDenyLocalRateLimiter : public LocalRateLimiter {
public:
  LocalRateLimiter::Result requestAllowed(absl::Span<const Envoy::RateLimit::Descriptor>) override {
    return {false, nullptr};
  }
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
