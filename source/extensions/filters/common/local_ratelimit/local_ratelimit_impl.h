#pragma once

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/ratelimit/ratelimit.h"

#include "common/common/thread_synchronizer.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

class LocalRateLimiterImpl {
public:
  LocalRateLimiterImpl(
      const std::chrono::milliseconds fill_interval, const uint32_t max_tokens,
      const uint32_t tokens_per_fill, Event::Dispatcher& dispatcher,
      const Protobuf::RepeatedPtrField<
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors);
  ~LocalRateLimiterImpl();

  bool requestAllowed(const std::vector<RateLimit::LocalDescriptor>& request_descriptors) const;

private:
  void onFillTimer();
  void onFillTimerHelper(const RateLimit::TokenState& state, const RateLimit::TokenBucket& bucket);
  void onFillTimerDescriptorHelper();
  bool requestAllowedHelper(const RateLimit::TokenState& tokens) const;

  RateLimit::TokenBucket token_bucket_;
  const Event::TimerPtr fill_timer_;
  TimeSource& time_source_;
  RateLimit::TokenState tokens_;
  absl::flat_hash_set<RateLimit::LocalDescriptor> descriptors_;
  std::vector<std::unique_ptr<RateLimit::TokenState>> descriptor_tokens_;
  mutable Thread::ThreadSynchronizer synchronizer_; // Used for testing only.

  friend class LocalRateLimiterImplTest;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
