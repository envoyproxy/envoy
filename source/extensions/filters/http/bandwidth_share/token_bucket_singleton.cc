#include "source/extensions/filters/http/bandwidth_share/token_bucket_singleton.h"

#include <atomic>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

SINGLETON_MANAGER_REGISTRATION(fair_token_bucket_singleton);

namespace {

uint64_t bytesPerSecond(const Runtime::UInt32& kbps_runtime_config) {
  // The proto/runtime value is in KiB/s; the token bucket accounts in bytes.
  return static_cast<uint64_t>(kbps_runtime_config.value()) * 1024;
}

} // namespace

class TokenBucketSingleton::BucketState {
public:
  BucketState(Runtime::UInt32&& max_tokens_runtime_config, uint32_t max_tokens_default_value,
              std::chrono::milliseconds fill_interval, TimeSource& time_source)
      : max_tokens_runtime_config_(std::move(max_tokens_runtime_config)),
        max_tokens_default_value_(max_tokens_default_value), fill_interval_(fill_interval),
        time_source_(time_source), max_tokens_(bytesPerSecond(max_tokens_runtime_config_)),
        bucket_(makeBucket(max_tokens_)) {}

  const Runtime::UInt32& maxTokensRuntimeConfig() const { return max_tokens_runtime_config_; }
  uint32_t maxTokensDefaultValue() const { return max_tokens_default_value_; }
  std::chrono::milliseconds fillInterval() const { return fill_interval_; }

  uint64_t bucketGeneration() const { return bucket_generation_.load(std::memory_order_acquire); }

  std::shared_ptr<FairTokenBucket::Bucket> bucketForRuntimeValue(uint64_t max_tokens_value,
                                                                 uint64_t& generation) {
    Thread::LockGuard lock(update_mu_);
    generation = bucket_generation_.load(std::memory_order_relaxed);
    if (max_tokens_ != max_tokens_value) {
      max_tokens_ = max_tokens_value;
      bucket_ = makeBucket(max_tokens_value);
      generation++;
      bucket_generation_.store(generation, std::memory_order_release);
    }
    return bucket_;
  }

private:
  std::shared_ptr<FairTokenBucket::Bucket> makeBucket(uint64_t max_tokens_value) const {
    return max_tokens_value == 0
               ? nullptr
               : FairTokenBucket::Bucket::create(max_tokens_value, time_source_, fill_interval_);
  }

  const Runtime::UInt32 max_tokens_runtime_config_;
  const uint32_t max_tokens_default_value_;
  const std::chrono::milliseconds fill_interval_;
  TimeSource& time_source_;
  Thread::MutexBasicLockable update_mu_;
  uint64_t max_tokens_ ABSL_GUARDED_BY(update_mu_);
  std::shared_ptr<FairTokenBucket::Bucket> bucket_ ABSL_GUARDED_BY(update_mu_);
  // Cheap invalidation signal for thread-local caches; avoids locking on unchanged requests.
  std::atomic<uint64_t> bucket_generation_{1};
};

class TokenBucketSingleton::ThreadLocalBuckets : public ThreadLocal::ThreadLocalObject {
public:
  void addBucket(std::string bucket_id, std::shared_ptr<BucketState> state) {
    const bool inserted = buckets_.try_emplace(std::move(bucket_id), std::move(state)).second;
    ASSERT(inserted);
  }

  std::shared_ptr<FairTokenBucket::Bucket> getBucket(absl::string_view bucket_id) {
    auto it = buckets_.find(bucket_id);
    // There is a code error if the entry is not found, since getBucket should
    // only ever be called with an id that has already been configured and therefore
    // passed to setBucket successfully.
    ASSERT(it != buckets_.end());
    return it->second.getBucket();
  }

private:
  struct Entry {
    explicit Entry(std::shared_ptr<BucketState> state) : state_(std::move(state)) {}

    std::shared_ptr<FairTokenBucket::Bucket> getBucket() {
      const uint64_t max_tokens_value = bytesPerSecond(state_->maxTokensRuntimeConfig());
      if (max_tokens_ == max_tokens_value && generation_ == state_->bucketGeneration()) {
        return bucket_;
      }
      bucket_ = state_->bucketForRuntimeValue(max_tokens_value, generation_);
      max_tokens_ = max_tokens_value;
      return bucket_;
    }

    std::shared_ptr<BucketState> state_;
    uint64_t generation_{};
    uint64_t max_tokens_{};
    std::shared_ptr<FairTokenBucket::Bucket> bucket_;
  };

  absl::flat_hash_map<std::string, Entry> buckets_;
};

TokenBucketSingleton::TokenBucketSingleton(TimeSource& time_source, Stats::Scope& scope,
                                           ThreadLocal::SlotAllocator& tls)
    : stat_names_(scope), time_source_(time_source), buckets_tls_(tls) {
  buckets_tls_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalBuckets>(); });
}

std::shared_ptr<TokenBucketSingleton>
TokenBucketSingleton::get(Singleton::Manager& singleton_manager, TimeSource& time_source,
                          Stats::Scope& stats_scope, ThreadLocal::SlotAllocator& tls) {
  return singleton_manager.getTyped<TokenBucketSingleton>(
      SINGLETON_MANAGER_REGISTERED_NAME(fair_token_bucket_singleton),
      [&time_source, &stats_scope, &tls] {
        return std::make_shared<TokenBucketSingleton>(time_source, stats_scope, tls);
      });
}

absl::Status TokenBucketSingleton::setBucket(absl::string_view bucket_id,
                                             Runtime::UInt32&& max_tokens_runtime_config,
                                             uint32_t max_tokens_default_value,
                                             std::chrono::milliseconds fill_interval) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto it = buckets_.find(bucket_id);
  if (it == buckets_.end()) {
    auto state =
        std::make_shared<BucketState>(std::move(max_tokens_runtime_config),
                                      max_tokens_default_value, fill_interval, time_source_);
    buckets_.emplace(bucket_id, state);
    buckets_tls_.runOnAllThreads([bucket_id = std::string(bucket_id),
                                  state = std::move(state)](OptRef<ThreadLocalBuckets> buckets) {
      ASSERT(buckets.has_value());
      buckets->addBucket(bucket_id, state);
    });
    return absl::OkStatus();
  }
  auto& state = *it->second;
  if (max_tokens_runtime_config.runtimeKey() != state.maxTokensRuntimeConfig().runtimeKey()) {
    return absl::InvalidArgumentError(
        absl::StrCat("bandwidth_share bucket ", bucket_id,
                     " attempted configuration with mismatched runtime config key ",
                     max_tokens_runtime_config.runtimeKey(), " vs. existing ",
                     state.maxTokensRuntimeConfig().runtimeKey(),
                     " - to have different config you must use a distinct bucket_id"));
  } else if (max_tokens_default_value != state.maxTokensDefaultValue()) {
    return absl::InvalidArgumentError(
        absl::StrCat("bandwidth_share bucket ", bucket_id,
                     " attempted configuration with mismatched default value ",
                     max_tokens_default_value, "KiB/s vs. existing ", state.maxTokensDefaultValue(),
                     "KiB/s - to have different config you must use a distinct bucket_id"));
  } else if (fill_interval != state.fillInterval()) {
    return absl::InvalidArgumentError(
        absl::StrCat("bandwidth_share bucket ", bucket_id,
                     " attempted configuration with mismatched fill_interval ",
                     fill_interval.count(), "ms vs. existing ", state.fillInterval().count(),
                     "ms - to have different config you must use a distinct bucket_id"));
  }
  return absl::OkStatus();
}

std::shared_ptr<FairTokenBucket::Bucket>
TokenBucketSingleton::getBucket(absl::string_view bucket_id) {
  auto buckets = buckets_tls_.get();
  ASSERT(buckets.has_value());
  return buckets->getBucket(bucket_id);
}

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
