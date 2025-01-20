#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include <chrono>
#include <cmath>
#include <memory>

#include "envoy/runtime/runtime.h"

#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

SINGLETON_MANAGER_REGISTRATION(local_ratelimit_share_provider_manager);

class DefaultEvenShareMonitor : public ShareProviderManager::ShareMonitor {
public:
  double getTokensShareFactor() const override { return share_factor_.load(); }
  double onLocalClusterUpdate(const Upstream::Cluster& cluster) override {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    const auto num = cluster.info()->endpointStats().membership_total_.value();
    const double new_share_factor = num == 0 ? 1.0 : 1.0 / num;
    share_factor_.store(new_share_factor);
    return new_share_factor;
  }

private:
  std::atomic<double> share_factor_{1.0};
};

ShareProviderManager::ShareProviderManager(Event::Dispatcher& main_dispatcher,
                                           const Upstream::Cluster& cluster)
    : main_dispatcher_(main_dispatcher), cluster_(cluster) {
  // It's safe to capture the local cluster reference here because the local cluster is
  // guaranteed to be static cluster and should never be removed.
  handle_ = cluster_.prioritySet().addMemberUpdateCb([this](const auto&, const auto&) {
    share_monitor_->onLocalClusterUpdate(cluster_);
    return absl::OkStatus();
  });
  share_monitor_ = std::make_shared<DefaultEvenShareMonitor>();
  share_monitor_->onLocalClusterUpdate(cluster_);
}

ShareProviderManager::~ShareProviderManager() {
  // Ensure the callback is unregistered on the main dispatcher thread.
  main_dispatcher_.post([h = std::move(handle_)]() {});
}

ShareProviderSharedPtr
ShareProviderManager::getShareProvider(const ProtoLocalClusterRateLimit&) const {
  // TODO(wbpcode): we may want to support custom share provider in the future based on the
  // configuration.
  return share_monitor_;
}

ShareProviderManagerSharedPtr ShareProviderManager::singleton(Event::Dispatcher& dispatcher,
                                                              Upstream::ClusterManager& cm,
                                                              Singleton::Manager& manager) {
  return manager.getTyped<ShareProviderManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(local_ratelimit_share_provider_manager),
      [&dispatcher, &cm]() -> Singleton::InstanceSharedPtr {
        const auto& local_cluster_name = cm.localClusterName();
        if (!local_cluster_name.has_value()) {
          return nullptr;
        }
        auto cluster = cm.clusters().getCluster(local_cluster_name.value());
        if (!cluster.has_value()) {
          return nullptr;
        }
        return ShareProviderManagerSharedPtr{
            new ShareProviderManager(dispatcher, cluster.value().get())};
      });
}

TimerTokenBucket::TimerTokenBucket(uint64_t max_tokens, uint64_t tokens_per_fill,
                                   std::chrono::milliseconds fill_interval, uint64_t multiplier,
                                   LocalRateLimiterImpl& parent)
    : multiplier_(multiplier), parent_(parent), max_tokens_(max_tokens),
      tokens_per_fill_(tokens_per_fill), fill_interval_(fill_interval),
      // Calculate the fill rate in tokens per second.
      fill_rate_(tokens_per_fill /
                 std::chrono::duration_cast<std::chrono::duration<double>>(fill_interval).count()) {
  ASSERT(multiplier_ != 0);
  tokens_ = max_tokens;
  fill_time_ = parent_.time_source_.monotonicTime();
}

absl::optional<int64_t> TimerTokenBucket::remainingFillInterval() const {
  using namespace std::literals;

  const auto time_after_last_fill = std::chrono::duration_cast<std::chrono::milliseconds>(
      parent_.time_source_.monotonicTime() - fill_time_.load());

  // Note that the fill timer may be delayed because other tasks are running on the main thread.
  // So it's possible that the time_after_last_fill is greater than fill_interval_.
  if (time_after_last_fill >= fill_interval_) {
    return {};
  }

  return absl::ToInt64Seconds(absl::FromChrono(fill_interval_) -
                              absl::Seconds((time_after_last_fill) / 1s));
}

bool TimerTokenBucket::consume(double, uint64_t to_consume) {
  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint64_t expected_tokens = tokens_.load(std::memory_order_relaxed);
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    if (expected_tokens < to_consume) {
      return false;
    }

    // Testing hook.
    parent_.synchronizer_.syncPoint("allowed_pre_cas");

    // Loop while the weak CAS fails trying to subtract tokens from expected.
  } while (!tokens_.compare_exchange_weak(expected_tokens, expected_tokens - to_consume,
                                          std::memory_order_relaxed));

  // We successfully decremented the counter by tokens.
  return true;
}

void TimerTokenBucket::onFillTimer(uint64_t refill_counter, double factor) {
  // Descriptors are refilled every Nth timer hit where N is the ratio of the
  // descriptor refill interval over the global refill interval. For example,
  // if the descriptor refill interval is 150ms and the global refill
  // interval is 50ms, this descriptor is refilled every 3rd call.
  ASSERT(multiplier_ != 0);
  if (refill_counter % multiplier_ != 0) {
    return;
  }

  const uint64_t tokens_per_fill = std::ceil(tokens_per_fill_ * factor);

  // Relaxed consistency is used for all operations because we don't care about ordering, just the
  // final atomic correctness.
  uint64_t expected_tokens = tokens_.load(std::memory_order_relaxed);
  uint64_t new_tokens_value{};
  do {
    // expected_tokens is either initialized above or reloaded during the CAS failure below.
    new_tokens_value = std::min(max_tokens_, expected_tokens + tokens_per_fill);

    // Testing hook.
    parent_.synchronizer_.syncPoint("on_fill_timer_pre_cas");

    // Loop while the weak CAS fails trying to update the tokens value.
  } while (
      !tokens_.compare_exchange_weak(expected_tokens, new_tokens_value, std::memory_order_relaxed));

  // Update fill time at last.
  fill_time_ = parent_.time_source_.monotonicTime();
}

AtomicTokenBucket::AtomicTokenBucket(uint64_t max_tokens, uint64_t tokens_per_fill,
                                     std::chrono::milliseconds fill_interval,
                                     TimeSource& time_source)
    : token_bucket_(max_tokens, time_source,
                    // Calculate the fill rate in tokens per second.
                    tokens_per_fill / std::chrono::duration<double>(fill_interval).count()),
      fill_interval_(fill_interval) {}

bool AtomicTokenBucket::consume(double factor, uint64_t to_consume) {
  ASSERT(!(factor <= 0.0 || factor > 1.0));
  auto cb = [tokens = to_consume / factor](double total) { return total < tokens ? 0.0 : tokens; };
  return token_bucket_.consume(cb) != 0.0;
}

LocalRateLimiterImpl::LocalRateLimiterImpl(
    const std::chrono::milliseconds fill_interval, const uint64_t max_tokens,
    const uint64_t tokens_per_fill, Event::Dispatcher& dispatcher,
    const Protobuf::RepeatedPtrField<
        envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors,
    bool always_consume_default_token_bucket, ShareProviderSharedPtr shared_provider,
    uint32_t lru_size, bool per_connection)
    : fill_timer_(fill_interval > std::chrono::milliseconds(0)
                      ? dispatcher.createTimer([this] { onFillTimer(); })
                      : nullptr),
      time_source_(dispatcher.timeSource()), share_provider_(std::move(shared_provider)),
      always_consume_default_token_bucket_(always_consume_default_token_bucket),
      no_timer_based_rate_limit_token_bucket_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.no_timer_based_rate_limit_token_bucket")) {
  if (fill_timer_ && fill_interval < std::chrono::milliseconds(50)) {
    throw EnvoyException("local rate limit token bucket fill timer must be >= 50ms");
  }

  if (no_timer_based_rate_limit_token_bucket_) {
    default_token_bucket_ = std::make_shared<AtomicTokenBucket>(max_tokens, tokens_per_fill,
                                                                fill_interval, time_source_);
  } else {
    default_token_bucket_ =
        std::make_shared<TimerTokenBucket>(max_tokens, tokens_per_fill, fill_interval, 1, *this);
  }

  if (fill_timer_ && default_token_bucket_->fillInterval().count() > 0 &&
      !no_timer_based_rate_limit_token_bucket_) {
    fill_timer_->enableTimer(default_token_bucket_->fillInterval());
  }

  for (const auto& descriptor : descriptors) {
    RateLimit::LocalDescriptor new_descriptor;
    bool wildcard_found = false;
    new_descriptor.entries_.reserve(descriptor.entries_size());
    for (const auto& entry : descriptor.entries()) {
      if (entry.value().empty()) {
        if (!(Runtime::runtimeFeatureEnabled(
                "envoy.reloadable_features.local_rate_limiting_with_dynamic_buckets"))) {
          throw EnvoyException("local_rate_limiting_with_dynamic_buckets is disabled. Local rate "
                               "descriptor value cannot be empty");
        }
        if (!no_timer_based_rate_limit_token_bucket_) {
          throw EnvoyException("local rate limit descriptor value cannot be empty without "
                               "no_timer_based_rate_limit_token_bucket");
        }

        if (lru_size == 0) {
          throw EnvoyException("minimum allowed value for max_dynamic_descriptors is 1");
        }

        if (per_connection) {
          throw EnvoyException(
              "local rate descriptor value cannot be empty in per connection rate limit mode");
        }
        wildcard_found = true;
      }
      new_descriptor.entries_.push_back({entry.key(), entry.value()});
    }

    const auto per_descriptor_max_tokens = descriptor.token_bucket().max_tokens();
    const auto per_descriptor_tokens_per_fill =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(descriptor.token_bucket(), tokens_per_fill, 1);
    const auto per_descriptor_fill_interval = std::chrono::milliseconds(
        PROTOBUF_GET_MS_OR_DEFAULT(descriptor.token_bucket(), fill_interval, 0));

    // Validate that the descriptor's fill interval is logically correct (same
    // constraint of >=50msec as for fill_interval).
    if (per_descriptor_fill_interval < std::chrono::milliseconds(50)) {
      throw EnvoyException("local rate limit descriptor token bucket fill timer must be >= 50ms");
    }

    if (per_descriptor_fill_interval.count() % fill_interval.count() != 0) {
      throw EnvoyException(
          "local rate descriptor limit is not a multiple of token bucket fill timer");
    }
    // Save the multiplicative factor to control the descriptor refill frequency.
    const auto per_descriptor_multiplier = per_descriptor_fill_interval / fill_interval;

    RateLimitTokenBucketSharedPtr per_descriptor_token_bucket;
    if (no_timer_based_rate_limit_token_bucket_) {
      per_descriptor_token_bucket = std::make_shared<AtomicTokenBucket>(
          per_descriptor_max_tokens, per_descriptor_tokens_per_fill, per_descriptor_fill_interval,
          time_source_);
    } else {
      per_descriptor_token_bucket = std::make_shared<TimerTokenBucket>(
          per_descriptor_max_tokens, per_descriptor_tokens_per_fill, per_descriptor_fill_interval,
          per_descriptor_multiplier, *this);
    }
    if (wildcard_found) {
      DynamicDescriptorSharedPtr dynamic_descriptor = std::make_shared<DynamicDescriptor>(
          per_descriptor_token_bucket, lru_size, dispatcher.timeSource());
      dynamic_descriptors_.addDescriptor(std::move(new_descriptor), std::move(dynamic_descriptor));
      continue;
    }
    auto result =
        descriptors_.emplace(std::move(new_descriptor), std::move(per_descriptor_token_bucket));
    if (!result.second) {
      throw EnvoyException(absl::StrCat("duplicate descriptor in the local rate descriptor: ",
                                        result.first->first.toString()));
    }
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
  const double share_factor =
      share_provider_ != nullptr ? share_provider_->getTokensShareFactor() : 1.0;

  default_token_bucket_->onFillTimer(refill_counter_, share_factor);
  for (const auto& descriptor : descriptors_) {
    descriptor.second->onFillTimer(refill_counter_, share_factor);
  }

  fill_timer_->enableTimer(default_token_bucket_->fillInterval());
}

struct MatchResult {
  RateLimitTokenBucketSharedPtr token_bucket;
  std::reference_wrapper<const RateLimit::Descriptor> request_descriptor;
};

LocalRateLimiterImpl::Result
LocalRateLimiterImpl::requestAllowed(absl::Span<const RateLimit::Descriptor> request_descriptors) {

  // In most cases the request descriptors has only few elements. We use a inlined vector to
  // avoid heap allocation.
  absl::InlinedVector<MatchResult, 8> matched_results;

  // Find all matched descriptors.
  for (const auto& request_descriptor : request_descriptors) {
    auto iter = descriptors_.find(request_descriptor);
    if (iter != descriptors_.end()) {
      matched_results.push_back(MatchResult{iter->second, request_descriptor});
    } else {
      auto token_bucket = dynamic_descriptors_.getBucket(request_descriptor);
      if (token_bucket != nullptr) {
        matched_results.push_back(MatchResult{token_bucket, request_descriptor});
      }
    }
  }

  if (matched_results.size() > 1) {
    // Sort the matched descriptors by token bucket fill rate to ensure the descriptor with the
    // smallest fill rate is consumed first.
    std::sort(matched_results.begin(), matched_results.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.token_bucket->fillRate() < rhs.token_bucket->fillRate();
    });
  }

  const double share_factor =
      share_provider_ != nullptr ? share_provider_->getTokensShareFactor() : 1.0;

  // See if the request is forbidden by any of the matched descriptors.
  for (const auto& match_result : matched_results) {
    if (!match_result.token_bucket->consume(
            share_factor, match_result.request_descriptor.get().hits_addend_.value_or(1))) {
      // If the request is forbidden by a descriptor, return the result and the descriptor
      // token bucket.
      return {false, std::shared_ptr<TokenBucketContext>(match_result.token_bucket)};
    }
    ENVOY_LOG(trace,
              "request allowed by descriptor with fill rate: {}, maxToken: {}, remainingToken {}",
              match_result.token_bucket->fillRate(), match_result.token_bucket->maxTokens(),
              match_result.token_bucket->remainingTokens());
  }

  // See if the request is forbidden by the default token bucket.
  if (matched_results.empty() || always_consume_default_token_bucket_) {
    if (const bool result = default_token_bucket_->consume(share_factor); !result) {
      // If the request is forbidden by the default token bucket, return the result and the
      // default token bucket.
      return {false, std::shared_ptr<TokenBucketContext>(default_token_bucket_)};
    }

    // If the request is allowed then return the result the token bucket. The descriptor
    // token bucket will be selected as priority if it exists.
    return {true,
            matched_results.empty() ? default_token_bucket_ : matched_results[0].token_bucket};
  };

  ASSERT(!matched_results.empty());
  std::shared_ptr<TokenBucketContext> bucket_context =
      std::shared_ptr<TokenBucketContext>(matched_results[0].token_bucket);
  return {true, bucket_context};
}

// Compare the request descriptor entries with the user descriptor entries. If all non-empty user
// descriptor values match the request descriptor values, return true
bool DynamicDescriptorMap::matchDescriptorEntries(
    const std::vector<RateLimit::DescriptorEntry>& request_entries,
    const std::vector<RateLimit::DescriptorEntry>& user_entries) {
  // Check for equality of sizes
  if (request_entries.size() != user_entries.size()) {
    return false;
  }

  bool has_empty_value = false;
  for (size_t i = 0; i < request_entries.size(); ++i) {
    // Check if the keys are equal
    if (request_entries[i].key_ != user_entries[i].key_) {
      return false;
    }

    // all non-blank user values must match the request values
    if (!user_entries[i].value_.empty() && user_entries[i].value_ != request_entries[i].value_) {
      return false;
    }

    // Check for empty value in user entries
    if (user_entries[i].value_.empty()) {
      has_empty_value = true;
    }
  }
  return has_empty_value;
}

void DynamicDescriptorMap::addDescriptor(const RateLimit::LocalDescriptor& user_descriptor,
                                         DynamicDescriptorSharedPtr dynamic_descriptor) {
  auto result = user_descriptors_.emplace(user_descriptor, std::move(dynamic_descriptor));
  if (!result.second) {
    throw EnvoyException(absl::StrCat("duplicate descriptor in the local rate descriptor: ",
                                      result.first->first.toString()));
  }
}

RateLimitTokenBucketSharedPtr
DynamicDescriptorMap::getBucket(const RateLimit::Descriptor request_descriptor) {
  for (const auto& pair : user_descriptors_) {
    auto user_descriptor = pair.first;
    if (!matchDescriptorEntries(request_descriptor.entries_, user_descriptor.entries_)) {
      continue;
    }

    // we found a user configured wildcard descriptor that matches the request descriptor.
    return pair.second->addOrGetDescriptor(request_descriptor);
  }
  return nullptr;
}

DynamicDescriptor::DynamicDescriptor(RateLimitTokenBucketSharedPtr token_bucket, uint32_t lru_size,
                                     TimeSource& time_source)
    : parent_token_bucket_(token_bucket), lru_size_(lru_size), time_source_(time_source) {}

RateLimitTokenBucketSharedPtr
DynamicDescriptor::addOrGetDescriptor(const RateLimit::Descriptor& request_descriptor) {
  absl::WriterMutexLock lock(&dyn_desc_lock_);
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
  ENVOY_LOG(trace, "max_tokens: {}, fill_rate: {}, fill_interval: {}",
            parent_token_bucket_->maxTokens(), parent_token_bucket_->fillRate(),
            std::chrono::duration<double>(parent_token_bucket_->fillInterval()).count());
  per_descriptor_token_bucket = std::make_shared<AtomicTokenBucket>(
      parent_token_bucket_->maxTokens(),
      uint32_t(parent_token_bucket_->fillRate() *
               std::chrono::duration<double>(parent_token_bucket_->fillInterval()).count()),
      parent_token_bucket_->fillInterval(), time_source_);

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
