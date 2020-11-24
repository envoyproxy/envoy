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
    const Envoy::Protobuf::RepeatedPtrField<
        envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors)
    : fill_interval_(fill_interval), max_tokens_(max_tokens), tokens_per_fill_(tokens_per_fill),
      fill_timer_(fill_interval_ > std::chrono::milliseconds(0)
                      ? dispatcher.createTimer([this] { onFillTimer(); })
                      : nullptr),
      time_source_(dispatcher.timeSource()) {
  if (fill_timer_ && fill_interval_ < std::chrono::milliseconds(50)) {
    throw EnvoyException("local rate limit token bucket fill timer must be >= 50ms");
  }

  tokens_.tokens = max_tokens;

  if (fill_timer_) {
    fill_timer_->enableTimer(fill_interval_);
  }

  for (const auto& descriptor : descriptors) {
    Envoy::RateLimit::LocalDescriptor new_descriptor;
    for (const auto& entry : descriptor.entries()) {
      new_descriptor.entries_.push_back({entry.key(), entry.value()});
    }
    Envoy::RateLimit::TokenBucket token_bucket;
    token_bucket.fill_interval_ =
        absl::Milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(descriptor.token_bucket(), fill_interval, 0));
    if (absl::ToChronoMilliseconds(token_bucket.fill_interval_).count() % fill_interval_.count() !=
        0) {
      throw EnvoyException(
          "local rate descriptor limit is not a multiple of token bucket fill timer");
    }
    token_bucket.max_tokens_ = descriptor.token_bucket().max_tokens();
    token_bucket.tokens_per_fill_ =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(descriptor.token_bucket(), tokens_per_fill, 1);
    new_descriptor.token_bucket_ = token_bucket;

    // Push to descriptors vector to maintain the ordering in which each
    // descriptor appeared in the config.
    descriptors_.push_back(new_descriptor);

    // Maintain the hash map of state of token bucket for each descriptor.
    std::unique_ptr<DescriptorTokenState> descriptor_state_token =
        std::make_unique<DescriptorTokenState>();
    // Fill with max_tokens first time.
    descriptor_state_token->token.tokens = token_bucket.max_tokens_;
    descriptor_state_token->monotonic_time = time_source_.monotonicTime();
    tokens_per_descriptor_[new_descriptor] = std::move(descriptor_state_token);
  }
}

LocalRateLimiterImpl::~LocalRateLimiterImpl() {
  if (fill_timer_ != nullptr) {
    fill_timer_->disableTimer();
  }
}

void LocalRateLimiterImpl::onFillTimer() {
  onFillTimerHelper(tokens_, max_tokens_, tokens_per_fill_);
  onFillTimerDescriptorHelper();
  fill_timer_->enableTimer(fill_interval_);
}

void LocalRateLimiterImpl::onFillTimerHelper(const Token& tokens, const uint32_t max_tokens,
                                             const uint32_t tokens_per_fill) {
  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint32_t expected_tokens = tokens.tokens.load(std::memory_order_relaxed);
  uint32_t new_tokens_value;
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    new_tokens_value = std::min(max_tokens, expected_tokens + tokens_per_fill);

    // Testing hook.
    synchronizer_.syncPoint("on_fill_timer_pre_cas");

    // Loop while the weak CAS fails trying to update the tokens value.
  } while (!tokens.tokens.compare_exchange_weak(expected_tokens, new_tokens_value,
                                                std::memory_order_relaxed));
}

void LocalRateLimiterImpl::onFillTimerDescriptorHelper() {
  auto current_time = time_source_.monotonicTime();
  for (const auto& descriptor : tokens_per_descriptor_) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(current_time -
                                                              descriptor.second->monotonic_time) >=
        absl::ToChronoMilliseconds(descriptor.first.token_bucket_.fill_interval_)) {

      onFillTimerHelper(descriptor.second->token, descriptor.first.token_bucket_.max_tokens_,
                        descriptor.first.token_bucket_.tokens_per_fill_);

      // Update the time.
      descriptor.second->monotonic_time = current_time;
    }
  }
}

bool LocalRateLimiterImpl::requestAllowedHelper(const Token& tokens) const {
  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint32_t expected_tokens = tokens.tokens.load(std::memory_order_relaxed);
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    if (expected_tokens == 0) {
      return false;
    }

    // Testing hook.
    synchronizer_.syncPoint("allowed_pre_cas");

    // Loop while the weak CAS fails trying to subtract 1 from expected.
  } while (!tokens.tokens.compare_exchange_weak(expected_tokens, expected_tokens - 1,
                                                std::memory_order_relaxed));

  // We successfully decremented the counter by 1.
  return true;
}

bool LocalRateLimiterImpl::requestAllowed(
    std::vector<Envoy::RateLimit::LocalDescriptor> route_descriptors) const {
  const Envoy::RateLimit::LocalDescriptor* descriptor = findDescriptor(route_descriptors);
  if (descriptor == nullptr) {
    return requestAllowedHelper(tokens_);
  }
  auto it = tokens_per_descriptor_.find(*descriptor);
  return requestAllowedHelper(it->second->token);
}

const Envoy::RateLimit::LocalDescriptor* LocalRateLimiterImpl::findDescriptor(
    std::vector<Envoy::RateLimit::LocalDescriptor> route_descriptors) const {
  if (descriptors_.empty() || route_descriptors.empty()) {
    return nullptr;
  }
  for (const auto& config_descriptor : descriptors_) {
    for (const auto& route_descriptor : route_descriptors) {
      if (std::equal(config_descriptor.entries_.begin(), config_descriptor.entries_.end(),
                     route_descriptor.entries_.begin())) {
        return &config_descriptor;
      }
    }
  }
  return nullptr;
}

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
