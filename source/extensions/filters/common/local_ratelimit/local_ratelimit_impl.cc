#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include <chrono>

#include "envoy/runtime/runtime.h"

#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

LocalRateLimiterImpl::LocalRateLimiterImpl(
    const std::chrono::milliseconds fill_interval, const uint32_t max_tokens,
    const uint32_t tokens_per_fill, Event::Dispatcher& dispatcher,
    const Protobuf::RepeatedPtrField<
        envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors,
    bool always_consume_default_token_bucket)
    : fill_timer_(fill_interval > std::chrono::milliseconds(0)
                      ? dispatcher.createTimer([this] { onFillTimer(); })
                      : nullptr),
      time_source_(dispatcher.timeSource()),
      always_consume_default_token_bucket_(always_consume_default_token_bucket) {
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
    RateLimit::TokenBucket per_descriptor_token_bucket;
    per_descriptor_token_bucket.fill_interval_ =
        absl::Milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(descriptor.token_bucket(), fill_interval, 0));
    if (per_descriptor_token_bucket.fill_interval_ % token_bucket_.fill_interval_ !=
        absl::ZeroDuration()) {
      throw EnvoyException(
          "local rate descriptor limit is not a multiple of token bucket fill timer");
    }
    // Save the multiplicative factor to control the descriptor refill frequency.
    new_descriptor.multiplier_ =
        per_descriptor_token_bucket.fill_interval_ / token_bucket_.fill_interval_;
    per_descriptor_token_bucket.max_tokens_ = descriptor.token_bucket().max_tokens();
    per_descriptor_token_bucket.tokens_per_fill_ =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(descriptor.token_bucket(), tokens_per_fill, 1);
    new_descriptor.token_bucket_ = per_descriptor_token_bucket;

    auto token_state = std::make_shared<TokenState>();
    token_state->tokens_ = per_descriptor_token_bucket.max_tokens_;
    token_state->fill_time_ = time_source_.monotonicTime();
    new_descriptor.token_state_ = token_state;

    auto result = descriptors_.emplace(new_descriptor);
    if (!result.second) {
      throw EnvoyException(absl::StrCat("duplicate descriptor in the local rate descriptor: ",
                                        result.first->toString()));
    }
    sorted_descriptors_.push_back(new_descriptor);
  }
  // If a request is limited by a descriptor, it should not consume tokens from the remaining
  // matched descriptors, so we sort the descriptors by tokens per second, as a result, in most
  // cases the strictest descriptor will be consumed first. However, it can not solve the
  // problem perfectly.
  if (!sorted_descriptors_.empty()) {
    std::sort(sorted_descriptors_.begin(), sorted_descriptors_.end(),
              [this](LocalDescriptorImpl a, LocalDescriptorImpl b) -> bool {
                const int a_token_fill_per_second = tokensFillPerSecond(a);
                const int b_token_fill_per_second = tokensFillPerSecond(b);
                return a_token_fill_per_second < b_token_fill_per_second;
              });
  }
}

LocalRateLimiterImpl::~LocalRateLimiterImpl() {
  if (fill_timer_ != nullptr) {
    fill_timer_->disableTimer();
  }
}

void LocalRateLimiterImpl::onFillTimer() {
  // Since descriptors tokens are refilled whenever the remainder of dividing refill_counter_
  // by descriptor.multiplier_ is zero and refill_counter_ is initialized to zero, it must be
  // incremented before executing the onFillTimerDescriptorHelper() method to prevent all
  // descriptors tokens from being refilled at the first time hit, regardless of its fill
  // interval configuration.
  refill_counter_++;
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
  for (const auto& descriptor : descriptors_) {
    // Descriptors are refilled every Nth timer hit where N is the ratio of the
    // descriptor refill interval over the global refill interval. For example,
    // if the descriptor refill interval is 150ms and the global refill
    // interval is 50ms, this descriptor is refilled every 3rd call.
    if (refill_counter_ % descriptor.multiplier_ == 0) {
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
  // Matched descriptors will be sorted by tokens per second and tokens consumed in order.
  // In most cases, if one of them is limited the remaining descriptors will not consume
  // their tokens.
  bool matched_descriptor = false;
  if (!descriptors_.empty() && !request_descriptors.empty()) {
    for (const auto& descriptor : sorted_descriptors_) {
      for (const auto& request_descriptor : request_descriptors) {
        if (descriptor == request_descriptor) {
          matched_descriptor = true;
          // Descriptor token is not enough.
          if (!requestAllowedHelper(*descriptor.token_state_)) {
            return false;
          }
          break;
        }
      }
    }
  }

  if (!matched_descriptor || always_consume_default_token_bucket_) {
    // Since global tokens are not sorted, it should be larger than other descriptors.
    return requestAllowedHelper(tokens_);
  }
  return true;
}

int LocalRateLimiterImpl::tokensFillPerSecond(LocalDescriptorImpl& descriptor) {
  return descriptor.token_bucket_.tokens_per_fill_ /
         (absl::ToInt64Seconds(descriptor.token_bucket_.fill_interval_)
              ? absl::ToInt64Seconds(descriptor.token_bucket_.fill_interval_)
              : 1);
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
