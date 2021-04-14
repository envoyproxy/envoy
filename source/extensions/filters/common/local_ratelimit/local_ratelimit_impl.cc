#include "extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

LocalRateLimiterImpl::LocalRateLimiterImpl(
    const std::chrono::milliseconds fill_interval, const uint32_t max_tokens,
    const uint32_t tokens_per_fill, Event::Dispatcher& dispatcher,
    const Protobuf::RepeatedPtrField<
        envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors)
    : fill_timer_(fill_interval > std::chrono::milliseconds(0)
                      ? dispatcher.createTimer([this] { onFillTimer(); })
                      : nullptr),
      time_source_(dispatcher.timeSource()) {
  if (fill_timer_ && fill_interval < std::chrono::milliseconds(50)) {
    throw EnvoyException("local rate limit token bucket fill timer must be >= 50ms");
  }

  token_bucket_.max_tokens_ = max_tokens;
  token_bucket_.tokens_per_fill_ = tokens_per_fill;
  token_bucket_.fill_interval_ = absl::FromChrono(fill_interval);
  tokens_.tokens_ = max_tokens;

  if (fill_timer_) {
    fill_timer_->enableTimer(fill_interval);
  }

  for (const auto& descriptor : descriptors) {
    LocalDescriptorImpl new_descriptor;
    for (const auto& entry : descriptor.entries()) {
      new_descriptor.entries_.push_back({entry.key(), entry.value()});
    }
    RateLimit::TokenBucket token_bucket;
    token_bucket.fill_interval_ =
        absl::Milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(descriptor.token_bucket(), fill_interval, 0));
    if (token_bucket.fill_interval_ % token_bucket_.fill_interval_ != absl::ZeroDuration()) {
      throw EnvoyException(
          "local rate descriptor limit is not a multiple of token bucket fill timer");
    }
    token_bucket.max_tokens_ = descriptor.token_bucket().max_tokens();
    token_bucket.tokens_per_fill_ =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(descriptor.token_bucket(), tokens_per_fill, 1);
    new_descriptor.token_bucket_ = token_bucket;

    auto token_state = std::make_unique<TokenState>();
    token_state->tokens_ = token_bucket.max_tokens_;
    token_state->fill_time_ = time_source_.monotonicTime();
    new_descriptor.token_state_ = std::move(token_state);

    auto result = descriptors_.emplace(std::move(new_descriptor));
    if (!result.second) {
      throw EnvoyException(absl::StrCat("duplicate descriptor in the local rate descriptor: ",
                                        result.first->toString()));
    }
  }
}

LocalRateLimiterImpl::~LocalRateLimiterImpl() {
  if (fill_timer_ != nullptr) {
    fill_timer_->disableTimer();
  }
}

void LocalRateLimiterImpl::onFillTimer() {
  onFillTimerHelper(tokens_, token_bucket_);
  onFillTimerDescriptorHelper();
  fill_timer_->enableTimer(absl::ToChronoMilliseconds(token_bucket_.fill_interval_));
}

void LocalRateLimiterImpl::onFillTimerHelper(const TokenState& tokens,
                                             const RateLimit::TokenBucket& bucket) {
  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint32_t expected_tokens = tokens.tokens_.load(std::memory_order_relaxed);
  uint32_t new_tokens_value;
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    new_tokens_value = std::min(bucket.max_tokens_, expected_tokens + bucket.tokens_per_fill_);

    // Testing hook.
    synchronizer_.syncPoint("on_fill_timer_pre_cas");

    // Loop while the weak CAS fails trying to update the tokens value.
  } while (!tokens.tokens_.compare_exchange_weak(expected_tokens, new_tokens_value,
                                                 std::memory_order_relaxed));
}

void LocalRateLimiterImpl::onFillTimerDescriptorHelper() {
  auto current_time = time_source_.monotonicTime();
  for (const auto& descriptor : descriptors_) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - descriptor.token_state_->fill_time_) >=
        absl::ToChronoMilliseconds(descriptor.token_bucket_.fill_interval_)) {
      onFillTimerHelper(*descriptor.token_state_, descriptor.token_bucket_);
      descriptor.token_state_->fill_time_ = current_time;
    }
  }
}

bool LocalRateLimiterImpl::requestAllowedHelper(const TokenState& tokens) const {
  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint32_t expected_tokens = tokens.tokens_.load(std::memory_order_relaxed);
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    if (expected_tokens == 0) {
      return false;
    }

    // Testing hook.
    synchronizer_.syncPoint("allowed_pre_cas");

    // Loop while the weak CAS fails trying to subtract 1 from expected.
  } while (!tokens.tokens_.compare_exchange_weak(expected_tokens, expected_tokens - 1,
                                                 std::memory_order_relaxed));

  // We successfully decremented the counter by 1.
  return true;
}

bool LocalRateLimiterImpl::requestAllowed(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  if (!descriptors_.empty() && !request_descriptors.empty()) {
    for (const auto& request_descriptor : request_descriptors) {
      auto it = descriptors_.find(request_descriptor);
      if (it != descriptors_.end()) {
        return requestAllowedHelper(*it->token_state_);
      }
    }
  }
  return requestAllowedHelper(tokens_);
}

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
