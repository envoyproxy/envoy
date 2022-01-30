#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include <chrono>

#include "source/common/protobuf/utility.h"

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
  tokens_.fill_time_ = time_source_.monotonicTime();

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

void LocalRateLimiterImpl::onFillTimerHelper(TokenState& tokens,
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

  // Update fill time at last.
  tokens.fill_time_ = time_source_.monotonicTime();
}

void LocalRateLimiterImpl::onFillTimerDescriptorHelper() {
  auto current_time = time_source_.monotonicTime();
  for (const auto& descriptor : descriptors_) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - descriptor.token_state_->fill_time_) >=
        absl::ToChronoMilliseconds(descriptor.token_bucket_.fill_interval_)) {
      onFillTimerHelper(*descriptor.token_state_, descriptor.token_bucket_);
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

OptRef<const LocalRateLimiterImpl::LocalDescriptorImpl> LocalRateLimiterImpl::descriptorHelper(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  if (!descriptors_.empty() && !request_descriptors.empty()) {
    // The override rate limit descriptor is selected by the first full match from the request
    // descriptors.
    for (const auto& request_descriptor : request_descriptors) {
      auto it = descriptors_.find(request_descriptor);
      if (it != descriptors_.end()) {
        return *it;
      }
    }
  }
  return {};
}

bool LocalRateLimiterImpl::requestAllowed(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  auto descriptor = descriptorHelper(request_descriptors);

  return descriptor.has_value() ? requestAllowedHelper(*descriptor.value().get().token_state_)
                                : requestAllowedHelper(tokens_);
}

uint32_t LocalRateLimiterImpl::maxTokens(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  auto descriptor = descriptorHelper(request_descriptors);

  return descriptor.has_value() ? descriptor.value().get().token_bucket_.max_tokens_
                                : token_bucket_.max_tokens_;
}

uint32_t LocalRateLimiterImpl::remainingTokens(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  auto descriptor = descriptorHelper(request_descriptors);

  return descriptor.has_value()
             ? descriptor.value().get().token_state_->tokens_.load(std::memory_order_relaxed)
             : tokens_.tokens_.load(std::memory_order_relaxed);
}

int64_t LocalRateLimiterImpl::remainingFillInterval(
    absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const {
  using namespace std::literals;

  auto current_time = time_source_.monotonicTime();
  auto descriptor = descriptorHelper(request_descriptors);
  // Remaining time to next fill = fill interval - (current time - last fill time).
  if (descriptor.has_value()) {
    ASSERT(std::chrono::duration_cast<std::chrono::milliseconds>(
               current_time - descriptor.value().get().token_state_->fill_time_) <=
           absl::ToChronoMilliseconds(descriptor.value().get().token_bucket_.fill_interval_));
    return absl::ToInt64Seconds(
        descriptor.value().get().token_bucket_.fill_interval_ -
        absl::Seconds((current_time - descriptor.value().get().token_state_->fill_time_) / 1s));
  }
  return absl::ToInt64Seconds(token_bucket_.fill_interval_ -
                              absl::Seconds((current_time - tokens_.fill_time_) / 1s));
}

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
