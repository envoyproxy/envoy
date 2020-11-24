#pragma once

#include <chrono>

#include "envoy/common/time.h"
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
      const Envoy::Protobuf::RepeatedPtrField<
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors);
  ~LocalRateLimiterImpl();

  bool requestAllowed(std::vector<Envoy::RateLimit::LocalDescriptor> route_descriptors) const;

private:
  struct Token {
    mutable std::atomic<uint32_t> tokens;
  };
  struct DescriptorTokenState {
    Token token;
    Envoy::MonotonicTime monotonic_time;
  };
  const Envoy::RateLimit::LocalDescriptor*
  findDescriptor(std::vector<Envoy::RateLimit::LocalDescriptor> descriptors) const;
  void onFillTimer();
  void onFillTimerHelper(const Token& tokens, const uint32_t max_tokens,
                         const uint32_t tokens_per_fill);
  void onFillTimerDescriptorHelper();
  bool requestAllowedHelper(const Token& tokens) const;

  const std::chrono::milliseconds fill_interval_;
  const uint32_t max_tokens_;
  const uint32_t tokens_per_fill_;
  const Event::TimerPtr fill_timer_;
  TimeSource& time_source_;
  Token tokens_;
  mutable Thread::ThreadSynchronizer synchronizer_; // Used for testing only.

  absl::flat_hash_map<Envoy::RateLimit::LocalDescriptor, std::unique_ptr<DescriptorTokenState>>
      tokens_per_descriptor_;

  std::vector<Envoy::RateLimit::LocalDescriptor> descriptors_;

  friend class LocalRateLimiterImplTest;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
