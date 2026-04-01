#pragma once

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/ratelimit/ratelimit.h"

#include "source/common/common/logger.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

class RateLimitTokenBucket;
using RateLimitTokenBucketSharedPtr = std::shared_ptr<RateLimitTokenBucket>;

class DynamicDescriptor : public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  DynamicDescriptor(uint64_t max_tokens, uint64_t tokens_per_fill,
                    std::chrono::milliseconds fill_interval, uint32_t lru_size,
                    TimeSource& time_source, bool shadow_mode);
  // add a new user configured descriptor to the set.
  RateLimitTokenBucketSharedPtr
  addOrGetDescriptor(const Envoy::RateLimit::Descriptor& request_descriptor);

private:
  using LruList = std::list<Envoy::RateLimit::Descriptor>;

  mutable absl::Mutex dyn_desc_lock_;
  Envoy::RateLimit::Descriptor::Map<std::pair<RateLimitTokenBucketSharedPtr, LruList::iterator>>
      dynamic_descriptors_ ABSL_GUARDED_BY(dyn_desc_lock_);

  uint64_t max_tokens_;
  uint64_t tokens_per_fill_;
  const std::chrono::milliseconds fill_interval_;
  LruList lru_list_;
  uint32_t lru_size_;
  TimeSource& time_source_;
  const bool shadow_mode_{false};
};

using DynamicDescriptorSharedPtr = std::shared_ptr<DynamicDescriptor>;

class DynamicDescriptorMap : public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  // add a new user configured descriptor to the set.
  void addDescriptor(const Envoy::RateLimit::LocalDescriptor& descriptor,
                     DynamicDescriptorSharedPtr dynamic_descriptor);
  // pass request_descriptors to the dynamic descriptor set to get the token bucket.
  RateLimitTokenBucketSharedPtr getBucket(const Envoy::RateLimit::Descriptor);

private:
  bool matchDescriptorEntries(const std::vector<Envoy::RateLimit::DescriptorEntry>& request_entries,
                              const std::vector<Envoy::RateLimit::DescriptorEntry>& user_entries);
  Envoy::RateLimit::LocalDescriptor::Map<DynamicDescriptorSharedPtr> config_descriptors_;
};

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
