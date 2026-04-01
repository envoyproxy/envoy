#include "source/extensions/filters/common/local_ratelimit/dynamic_descriptor.h"

#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

// Compare the request descriptor entries with the user descriptor entries. If all non-empty user
// descriptor values match the request descriptor values, return true
bool DynamicDescriptorMap::matchDescriptorEntries(
    const std::vector<RateLimit::DescriptorEntry>& request_entries,
    const std::vector<RateLimit::DescriptorEntry>& config_entries) {
  // Check for equality of sizes
  if (request_entries.size() != config_entries.size()) {
    return false;
  }

  for (size_t i = 0; i < request_entries.size(); ++i) {
    // Check if the keys are equal.
    if (request_entries[i].key_ != config_entries[i].key_) {
      return false;
    }

    // Check values are equal or wildcard value is used.
    if (config_entries[i].value_.empty()) {
      continue;
    }
    if (request_entries[i].value_ != config_entries[i].value_) {
      return false;
    }
  }
  return true;
}

void DynamicDescriptorMap::addDescriptor(const RateLimit::LocalDescriptor& config_descriptor,
                                         DynamicDescriptorSharedPtr dynamic_descriptor) {
  auto result = config_descriptors_.emplace(config_descriptor, std::move(dynamic_descriptor));
  if (!result.second) {
    throw EnvoyException(absl::StrCat("duplicate descriptor in the local rate descriptor: ",
                                      result.first->first.toString()));
  }
}

RateLimitTokenBucketSharedPtr
DynamicDescriptorMap::getBucket(const RateLimit::Descriptor request_descriptor) {
  for (const auto& pair : config_descriptors_) {
    auto config_descriptor = pair.first;
    if (!matchDescriptorEntries(request_descriptor.entries_, config_descriptor.entries_)) {
      continue;
    }

    // here is when a user configured wildcard descriptor matches the request descriptor.
    return pair.second->addOrGetDescriptor(request_descriptor);
  }
  return nullptr;
}

DynamicDescriptor::DynamicDescriptor(uint64_t per_descriptor_max_tokens,
                                     uint64_t per_descriptor_tokens_per_fill,
                                     std::chrono::milliseconds per_descriptor_fill_interval,
                                     uint32_t lru_size, TimeSource& time_source, bool shadow_mode)
    : max_tokens_(per_descriptor_max_tokens), tokens_per_fill_(per_descriptor_tokens_per_fill),
      fill_interval_(per_descriptor_fill_interval), lru_size_(lru_size), time_source_(time_source),
      shadow_mode_(shadow_mode) {}

RateLimitTokenBucketSharedPtr
DynamicDescriptor::addOrGetDescriptor(const RateLimit::Descriptor& request_descriptor) {
  absl::WriterMutexLock lock(dyn_desc_lock_);
  auto iter = dynamic_descriptors_.find(request_descriptor);
  if (iter != dynamic_descriptors_.end()) {
    if (iter->second.second != lru_list_.begin()) {
      lru_list_.splice(lru_list_.begin(), lru_list_, iter->second.second);
    }
    return iter->second.first;
  }
  // add a new descriptor to the set along with its token bucket
  RateLimitTokenBucketSharedPtr per_descriptor_token_bucket;
  ENVOY_LOG(trace, "creating atomic token bucket for dynamic descriptor");
  ENVOY_LOG(trace, "max_tokens: {}, tokens_per_fill: {}, fill_interval: {}", max_tokens_,
            tokens_per_fill_, std::chrono::duration<double>(fill_interval_).count());
  per_descriptor_token_bucket = std::make_shared<RateLimitTokenBucket>(
      max_tokens_, tokens_per_fill_, fill_interval_, time_source_, shadow_mode_);

  ENVOY_LOG(trace, "DynamicDescriptor::addorGetDescriptor: adding dynamic descriptor: {}",
            request_descriptor.toString());
  lru_list_.emplace_front(request_descriptor);
  auto result = dynamic_descriptors_.emplace(
      request_descriptor, std::pair(per_descriptor_token_bucket, lru_list_.begin()));
  auto token_bucket = result.first->second.first;
  if (lru_list_.size() > lru_size_) {
    ENVOY_LOG(trace,
              "DynamicDescriptor::addorGetDescriptor: lru_size({}) overflow. Removing dynamic "
              "descriptor: {}",
              lru_size_, lru_list_.back().toString());
    dynamic_descriptors_.erase(lru_list_.back());
    lru_list_.pop_back();
  }
  ASSERT(lru_list_.size() == dynamic_descriptors_.size());
  return token_bucket;
}

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
