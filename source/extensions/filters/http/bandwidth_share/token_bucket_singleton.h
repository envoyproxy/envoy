#pragma once

#include <memory>
#include <string>

#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"

#include "source/common/common/thread.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/http/bandwidth_share/fair_token_bucket_impl.h"
#include "source/extensions/filters/http/bandwidth_share/stats.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

class TokenBucketSingleton : public Singleton::Instance {
public:
  TokenBucketSingleton(TimeSource& time_source, Stats::Scope& scope)
      : stat_names_(scope), time_source_(time_source) {}

  // If the returned singleton becomes un-owned, the singleton manager will
  // generate a new one if get is called afterwards.
  static std::shared_ptr<TokenBucketSingleton>
  get(Singleton::Manager& singleton_manager, TimeSource& time_source, Stats::Scope& stats_scope);

  // Adds a bucket to the singleton. If the bucket_id already exists and
  // has a different runtime_config_ key, an error is returned.
  absl::Status setBucket(absl::string_view bucket_id, Runtime::UInt32&& max_tokens_runtime_config,
                         std::chrono::milliseconds fill_interval);

  // Retrieves a bucket with the given id. Should never be called with
  // a nonexistent bucket id, as config should always have called setBucket.
  //
  // If max_tokens is zero, returns a nullptr.
  //
  // The returned bucket can potentially be replaced between calls on the
  // same request, if the runtime token limit is altered - for this reason
  // a shared_ptr is returned rather than a reference, so clients can
  // continue to use the instance they received while a new one replaces it.
  std::shared_ptr<FairTokenBucket::Bucket> getBucket(absl::string_view bucket_id);

  BandwidthShareStatNames stat_names_;

private:
  Thread::MutexBasicLockable mu_;
  struct Entry {
    Entry(std::shared_ptr<FairTokenBucket::Bucket> bucket, uint32_t max_tokens,
          std::chrono::milliseconds fill_interval, Runtime::UInt32&& max_tokens_runtime_config)
        : bucket_(std::move(bucket)), max_tokens_(max_tokens), fill_interval_(fill_interval),
          max_tokens_runtime_config_(std::move(max_tokens_runtime_config)) {}
    std::shared_ptr<FairTokenBucket::Bucket> bucket_;
    uint32_t max_tokens_ = 0;
    std::chrono::milliseconds fill_interval_ = std::chrono::milliseconds{};
    const Runtime::UInt32 max_tokens_runtime_config_;
  };
  TimeSource& time_source_;
  absl::flat_hash_map<std::string, Entry> buckets_ ABSL_GUARDED_BY(mu_);
};

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
