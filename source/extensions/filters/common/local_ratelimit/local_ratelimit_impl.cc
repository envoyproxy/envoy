#include "extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

LocalRateLimiterImpl::LocalRateLimiterImpl(const std::chrono::milliseconds fill_interval,
                                           const uint32_t max_tokens,
                                           const uint32_t tokens_per_fill,
                                           Event::Dispatcher& dispatcher)
    : fill_interval_(fill_interval), max_tokens_(max_tokens), tokens_per_fill_(tokens_per_fill),
      fill_timer_(fill_interval_ > std::chrono::milliseconds(0)
                      ? dispatcher.createTimer([this] { onFillTimer(); })
                      : nullptr) {
  if (fill_timer_ && fill_interval_ < std::chrono::milliseconds(50)) {
    throw EnvoyException("local rate limit token bucket fill timer must be >= 50ms");
  }

  tokens_ = max_tokens;

  if (fill_timer_) {
    fill_timer_->enableTimer(fill_interval_);
  }
}

LocalRateLimiterImpl::~LocalRateLimiterImpl() {
  if (fill_timer_ != nullptr) {
    fill_timer_->disableTimer();
  }
}

void LocalRateLimiterImpl::onFillTimer() {
  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint32_t expected_tokens = tokens_.load(std::memory_order_relaxed);
  uint32_t new_tokens_value;
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    new_tokens_value = std::min(max_tokens_, expected_tokens + tokens_per_fill_);

    // Testing hook.
    synchronizer_.syncPoint("on_fill_timer_pre_cas");

    // Loop while the weak CAS fails trying to update the tokens value.
  } while (
      !tokens_.compare_exchange_weak(expected_tokens, new_tokens_value, std::memory_order_relaxed));

  fill_timer_->enableTimer(fill_interval_);
}

bool LocalRateLimiterImpl::requestAllowed() const {
  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint32_t expected_tokens = tokens_.load(std::memory_order_relaxed);
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    if (expected_tokens == 0) {
      return false;
    }

    // Testing hook.
    synchronizer_.syncPoint("allowed_pre_cas");

    // Loop while the weak CAS fails trying to subtract 1 from expected.
  } while (!tokens_.compare_exchange_weak(expected_tokens, expected_tokens - 1,
                                          std::memory_order_relaxed));

  // We successfully decremented the counter by 1.
  return true;
}

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
