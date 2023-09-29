#pragma once

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/ratelimit/ratelimit.h"

#include "source/common/common/thread_synchronizer.h"
#include "source/common/protobuf/protobuf.h"

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
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors,
      bool always_consume_default_token_bucket = true);
  ~LocalRateLimiterImpl();

  bool requestAllowed(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const;
  uint32_t maxTokens(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const;
  uint32_t remainingTokens(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const;
  int64_t
  remainingFillInterval(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const;

private:
  struct TokenState {
    mutable std::atomic<uint32_t> tokens_;
    MonotonicTime fill_time_;
  };
  // Refill counter is incremented per each refill timer hit.
  uint64_t refill_counter_{0};
  struct LocalDescriptorImpl : public RateLimit::LocalDescriptor {
    std::shared_ptr<TokenState> token_state_;
    RateLimit::TokenBucket token_bucket_;
    // Descriptor refill interval is a multiple of the timer refill interval.
    // For example, if the descriptor refill interval is 150ms and the global
    // refill interval is 50ms, the value is 3. Every 3rd invocation of
    // the global timer, the descriptor is refilled.
    uint64_t multiplier_;
    std::string toString() const {
      std::vector<std::string> entries;
      entries.reserve(entries_.size());
      for (const auto& entry : entries_) {
        entries.push_back(absl::StrCat(entry.key_, "=", entry.value_));
      }
      return absl::StrJoin(entries, ", ");
    }
  };
  struct LocalDescriptorHash {
    using is_transparent = void; // NOLINT(readability-identifier-naming)
    size_t operator()(const RateLimit::LocalDescriptor& d) const {
      return absl::Hash<std::vector<RateLimit::DescriptorEntry>>()(d.entries_);
    }
  };
  struct LocalDescriptorEqual {
    using is_transparent = void; // NOLINT(readability-identifier-naming)
    size_t operator()(const RateLimit::LocalDescriptor& a,
                      const RateLimit::LocalDescriptor& b) const {
      return a.entries_ == b.entries_;
    }
  };

  void onFillTimer();
  void onFillTimerHelper(TokenState& state, const RateLimit::TokenBucket& bucket);
  void onFillTimerDescriptorHelper();
  OptRef<const LocalDescriptorImpl>
  descriptorHelper(absl::Span<const RateLimit::LocalDescriptor> request_descriptors) const;
  bool requestAllowedHelper(const TokenState& tokens) const;
  int tokensFillPerSecond(LocalDescriptorImpl& descriptor);

  RateLimit::TokenBucket token_bucket_;
  const Event::TimerPtr fill_timer_;
  TimeSource& time_source_;
  TokenState tokens_;
  absl::flat_hash_set<LocalDescriptorImpl, LocalDescriptorHash, LocalDescriptorEqual> descriptors_;
  std::vector<LocalDescriptorImpl> sorted_descriptors_;
  mutable Thread::ThreadSynchronizer synchronizer_; // Used for testing only.
  const bool always_consume_default_token_bucket_{};

  friend class LocalRateLimiterImplTest;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
