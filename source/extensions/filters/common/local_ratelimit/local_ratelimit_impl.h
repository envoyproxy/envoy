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

  virtual uint64_t maxTokens() const PURE;
  virtual uint64_t remainingTokens() const PURE;
};

class LocalRateLimiterImpl;

class RateLimitTokenBucket : public TokenBucketContext {
public:
  RateLimitTokenBucket(uint64_t max_tokens, uint64_t tokens_per_fill,
                       std::chrono::milliseconds fill_interval, TimeSource& time_source);

  // RateLimitTokenBucket
  bool consume(double factor = 1.0, uint64_t tokens = 1);
  double fillRate() const { return token_bucket_.fillRate(); }

  uint64_t maxTokens() const override { return static_cast<uint64_t>(token_bucket_.maxTokens()); }
  uint64_t remainingTokens() const override {
    return static_cast<uint64_t>(token_bucket_.remainingTokens());
  }

private:
  AtomicTokenBucketImpl token_bucket_;
};
using RateLimitTokenBucketSharedPtr = std::shared_ptr<RateLimitTokenBucket>;

class LocalRateLimiterImpl {
public:
  struct Result {
    bool allowed{};
    OptRef<const TokenBucketContext> token_bucket_context{};
  };

  LocalRateLimiterImpl(
      const std::chrono::milliseconds fill_interval, const uint64_t max_tokens,
      const uint64_t tokens_per_fill, Event::Dispatcher& dispatcher,
      const Protobuf::RepeatedPtrField<
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors,
      bool always_consume_default_token_bucket = true,
      ShareProviderSharedPtr shared_provider = nullptr);
  ~LocalRateLimiterImpl();

  Result requestAllowed(absl::Span<const RateLimit::Descriptor> request_descriptors) const;

private:
  RateLimitTokenBucketSharedPtr default_token_bucket_;

  TimeSource& time_source_;
  RateLimit::LocalDescriptor::Map<RateLimitTokenBucketSharedPtr> descriptors_;

  ShareProviderSharedPtr share_provider_;

  mutable Thread::ThreadSynchronizer synchronizer_; // Used for testing only.
  const bool always_consume_default_token_bucket_{};
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
