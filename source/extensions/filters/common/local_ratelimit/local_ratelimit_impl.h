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
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors);
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
  struct LocalDescriptorImpl : public RateLimit::LocalDescriptor {
    std::unique_ptr<TokenState> token_state_;
    RateLimit::TokenBucket token_bucket_;
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

  RateLimit::TokenBucket token_bucket_;
  const Event::TimerPtr fill_timer_;
  TimeSource& time_source_;
  TokenState tokens_;
  absl::flat_hash_set<LocalDescriptorImpl, LocalDescriptorHash, LocalDescriptorEqual> descriptors_;
  mutable Thread::ThreadSynchronizer synchronizer_; // Used for testing only.

  friend class LocalRateLimiterImplTest;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
