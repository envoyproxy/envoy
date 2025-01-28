#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include <chrono>
#include <cmath>

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

RateLimitTokenBucket::RateLimitTokenBucket(uint64_t max_tokens, uint64_t tokens_per_fill,
                                           std::chrono::milliseconds fill_interval,
                                           TimeSource& time_source)
    : token_bucket_(max_tokens, time_source,
                    // Calculate the fill rate in tokens per second.
                    tokens_per_fill / std::chrono::duration<double>(fill_interval).count()) {}

bool RateLimitTokenBucket::consume(double factor, uint64_t to_consume) {
  ASSERT(!(factor <= 0.0 || factor > 1.0));
  auto cb = [tokens = to_consume / factor](double total) { return total < tokens ? 0.0 : tokens; };
  return token_bucket_.consume(cb) != 0.0;
}

LocalRateLimiterImpl::LocalRateLimiterImpl(
    const std::chrono::milliseconds fill_interval, const uint64_t max_tokens,
    const uint64_t tokens_per_fill, Event::Dispatcher& dispatcher,
    const Protobuf::RepeatedPtrField<
        envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors,
    bool always_consume_default_token_bucket, ShareProviderSharedPtr shared_provider)
    : time_source_(dispatcher.timeSource()), share_provider_(std::move(shared_provider)),
      always_consume_default_token_bucket_(always_consume_default_token_bucket) {

  // Ignore the default token bucket if fill_interval is 0 because 0 fill_interval means nothing
  // and has undefined behavior.
  if (fill_interval.count() > 0) {
    if (fill_interval < std::chrono::milliseconds(50)) {
      throw EnvoyException("local rate limit token bucket fill timer must be >= 50ms");
    }
    default_token_bucket_ = std::make_shared<RateLimitTokenBucket>(max_tokens, tokens_per_fill,
                                                                   fill_interval, time_source_);
  }

  for (const auto& descriptor : descriptors) {
    RateLimit::LocalDescriptor new_descriptor;
    new_descriptor.entries_.reserve(descriptor.entries_size());
    for (const auto& entry : descriptor.entries()) {
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

    RateLimitTokenBucketSharedPtr per_descriptor_token_bucket =
        std::make_shared<RateLimitTokenBucket>(per_descriptor_max_tokens,
                                               per_descriptor_tokens_per_fill,
                                               per_descriptor_fill_interval, time_source_);

    auto result =
        descriptors_.emplace(std::move(new_descriptor), std::move(per_descriptor_token_bucket));
    if (!result.second) {
      throw EnvoyException(absl::StrCat("duplicate descriptor in the local rate descriptor: ",
                                        result.first->first.toString()));
    }
  }
}

LocalRateLimiterImpl::~LocalRateLimiterImpl() = default;

struct MatchResult {
  std::reference_wrapper<RateLimitTokenBucket> token_bucket;
  std::reference_wrapper<const RateLimit::Descriptor> request_descriptor;
};

LocalRateLimiterImpl::Result LocalRateLimiterImpl::requestAllowed(
    absl::Span<const RateLimit::Descriptor> request_descriptors) const {

  // In most cases the request descriptors has only few elements. We use a inlined vector to
  // avoid heap allocation.
  absl::InlinedVector<MatchResult, 8> matched_results;

  // Find all matched descriptors.
  for (const auto& request_descriptor : request_descriptors) {
    auto iter = descriptors_.find(request_descriptor);
    if (iter != descriptors_.end()) {
      matched_results.push_back(MatchResult{*iter->second, request_descriptor});
    }
  }

  if (matched_results.size() > 1) {
    // Sort the matched descriptors by token bucket fill rate to ensure the descriptor with the
    // smallest fill rate is consumed first.
    std::sort(matched_results.begin(), matched_results.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.token_bucket.get().fillRate() < rhs.token_bucket.get().fillRate();
    });
  }

  const double share_factor =
      share_provider_ != nullptr ? share_provider_->getTokensShareFactor() : 1.0;

  // See if the request is forbidden by any of the matched descriptors.
  for (auto match_result : matched_results) {
    if (!match_result.token_bucket.get().consume(
            share_factor, match_result.request_descriptor.get().hits_addend_.value_or(1))) {
      // If the request is forbidden by a descriptor, return the result and the descriptor
      // token bucket.
      return {false, makeOptRef<TokenBucketContext>(match_result.token_bucket.get())};
    }
  }

  // See if the request is forbidden by the default token bucket.
  if (matched_results.empty() || always_consume_default_token_bucket_) {
    if (default_token_bucket_ == nullptr) {
      return {true, matched_results.empty()
                        ? makeOptRefFromPtr<TokenBucketContext>(nullptr)
                        : makeOptRef<TokenBucketContext>(matched_results[0].token_bucket.get())};
    }
    ASSERT(default_token_bucket_ != nullptr);

    if (const bool result = default_token_bucket_->consume(share_factor); !result) {
      // If the request is forbidden by the default token bucket, return the result and the
      // default token bucket.
      return {false, makeOptRefFromPtr<TokenBucketContext>(default_token_bucket_.get())};
    }

    // If the request is allowed then return the result the token bucket. The descriptor
    // token bucket will be selected as priority if it exists.
    return {true, makeOptRef<TokenBucketContext>(matched_results.empty()
                                                     ? *default_token_bucket_
                                                     : matched_results[0].token_bucket.get())};
  };

  ASSERT(!matched_results.empty());
  return {true, makeOptRef<TokenBucketContext>(matched_results[0].token_bucket.get())};
}

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
