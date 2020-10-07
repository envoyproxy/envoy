#pragma once

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "common/common/thread_synchronizer.h"

#include "extensions/filters/common/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

class LocalRateLimiterImpl : public LocalRateLimiter {
public:
  LocalRateLimiterImpl(const std::chrono::milliseconds fill_interval, const uint32_t max_tokens,
                       const uint32_t tokens_per_fill, Event::Dispatcher& dispatcher);
  ~LocalRateLimiterImpl() override;

  // Filters::Common::LocalRateLimit::LocalRateLimiter
  bool requestAllowed() const override;

private:
  void onFillTimer();

  const std::chrono::milliseconds fill_interval_;
  const uint32_t max_tokens_;
  const uint32_t tokens_per_fill_;
  const Event::TimerPtr fill_timer_;
  mutable std::atomic<uint32_t> tokens_;
  mutable Thread::ThreadSynchronizer synchronizer_; // Used for testing only.

  friend class LocalRateLimiterImplTest;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
