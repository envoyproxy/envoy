#include "source/extensions/filters/http/bandwidth_share/token_bucket_singleton.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

SINGLETON_MANAGER_REGISTRATION(fair_token_bucket_singleton);

std::shared_ptr<TokenBucketSingleton>
TokenBucketSingleton::get(Singleton::Manager& singleton_manager, TimeSource& time_source,
                          Stats::Scope& stats_scope) {
  return singleton_manager.getTyped<TokenBucketSingleton>(
      SINGLETON_MANAGER_REGISTERED_NAME(fair_token_bucket_singleton), [&time_source, &stats_scope] {
        return std::make_shared<TokenBucketSingleton>(time_source, stats_scope);
      });
}

absl::Status TokenBucketSingleton::setBucket(absl::string_view bucket_id,
                                             Runtime::UInt32&& max_tokens_runtime_config,
                                             std::chrono::milliseconds fill_interval) {
  Thread::LockGuard lock(mu_);
  auto it = buckets_.find(bucket_id);
  if (it == buckets_.end()) {
    uint32_t max_tokens_value = max_tokens_runtime_config.value();
    buckets_.emplace(
        bucket_id,
        Entry{FairTokenBucket::Bucket::create(max_tokens_value, time_source_, fill_interval),
              max_tokens_value, fill_interval, std::move(max_tokens_runtime_config)});
    return absl::OkStatus();
  }
  auto& entry = it->second;
  if (max_tokens_runtime_config.runtimeKey() != entry.max_tokens_runtime_config_.runtimeKey()) {
    return absl::InvalidArgumentError(
        absl::StrCat("bandwidth_share bucket ", bucket_id,
                     " attempted configuration with mismatched runtime config key ",
                     max_tokens_runtime_config.runtimeKey(), " vs. existing ",
                     entry.max_tokens_runtime_config_.runtimeKey(),
                     " - to have different config you must use a distinct bucket_id"));
  } else if (fill_interval != entry.fill_interval_) {
    return absl::InvalidArgumentError(
        absl::StrCat("bandwidth_share bucket ", bucket_id,
                     " attempted configuration with mismatched fill_interval ",
                     fill_interval.count(), "ms vs. existing ", entry.fill_interval_.count(),
                     "ms - to have different config you must use a distinct bucket_id"));
  }
  return absl::OkStatus();
}

std::shared_ptr<FairTokenBucket::Bucket>
TokenBucketSingleton::getBucket(absl::string_view bucket_id) {
  Thread::LockGuard lock(mu_);
  auto it = buckets_.find(bucket_id);
  // There is a code error if the entry is not found, since getFactory should
  // only ever be called with an id that has already been configured and therefore
  // passed to setFactory successfully.
  ASSERT(it != buckets_.end());
  auto& entry = it->second;
  uint32_t max_tokens_value = entry.max_tokens_runtime_config_.value();
  if (entry.max_tokens_ != max_tokens_value) {
    // Runtime value of the bucket size has changed, replace the bucket.
    entry.max_tokens_ = max_tokens_value;
    entry.bucket_ =
        FairTokenBucket::Bucket::create(max_tokens_value, time_source_, entry.fill_interval_);
  }
  if (entry.max_tokens_ == 0) {
    return nullptr;
  }
  return entry.bucket_;
}

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
