#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

#include "envoy/runtime/runtime.h"

#include "source/common/config/metadata.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

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

// Node metadata namespace and key carrying this instance's own load balancing weight.
constexpr absl::string_view SelfWeightMetadataNamespace = "envoy.local_ratelimit";
constexpr absl::string_view SelfWeightMetadataKey = "self_weight";

// Reports whether this instance's own weight could be read from node metadata. A gauge rather than
// a counter, because this is a steady state of the instance and not an event: an operator wants to
// know that some instance in the fleet is dividing the bucket by a weight it was never given, which
// a counter incremented once at startup makes needlessly hard to ask.
constexpr absl::string_view SelfWeightNotFoundGauge =
    "local_rate_limit.local_cluster_share.self_weight_not_found";

// Reads this instance's own weight out of node metadata. Absent, non-numeric and non-positive
// values are all treated as absent; a weight is a positive number or it is not a weight.
absl::optional<uint64_t> selfWeightFromNodeMetadata(const LocalInfo::LocalInfo& local_info) {
  const auto& value = Config::Metadata::structValue(
      local_info.node().metadata(),
      {std::string(SelfWeightMetadataNamespace), std::string(SelfWeightMetadataKey)});
  if (value.kind_case() != Protobuf::Value::kNumberValue) {
    return absl::nullopt;
  }
  const double weight = value.number_value();
  if (!std::isfinite(weight) || weight < 1.0 ||
      weight > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    return absl::nullopt;
  }
  return static_cast<uint64_t>(weight);
}

// Divides the tokens in proportion to this instance's load balancing weight, so that a local
// cluster with heterogeneous weights gives each instance a share of the bucket that matches its
// share of the traffic.
//
// The total weight has to come from the local cluster, because it changes as the cluster's
// membership changes. This instance's own weight does not: it is supplied by whoever assigned it,
// through node metadata. Deriving it from the cluster instead would mean recognizing this Envoy's
// own endpoint among the cluster's endpoints, which nothing in an endpoint reliably identifies --
// an address is not unique to an instance, and an endpoint hostname is optional in EDS.
class WeightedShareMonitor : public ShareProviderManager::ShareMonitor,
                             public Logger::Loggable<Logger::Id::local_rate_limit> {
public:
  WeightedShareMonitor(const LocalInfo::LocalInfo& local_info, Stats::Scope& scope)
      : self_weight_(selfWeightFromNodeMetadata(local_info)),
        self_weight_not_found_(scope.gaugeFromString(std::string(SelfWeightNotFoundGauge),
                                                     Stats::Gauge::ImportMode::NeverImport)) {
    self_weight_not_found_.set(self_weight_.has_value() ? 0 : 1);
    if (self_weight_.has_value()) {
      ENVOY_LOG(info, "local cluster rate limit: weighted share using own weight {}",
                *self_weight_);
    } else {
      ENVOY_LOG(warn,
                "local cluster rate limit: weighted share mode is configured but node metadata "
                "'{}.{}' does not hold a positive weight for this instance; falling back to the "
                "smallest weight in the local cluster",
                SelfWeightMetadataNamespace, SelfWeightMetadataKey);
    }
  }

  double getTokensShareFactor() const override { return share_factor_.load(); }

  double onLocalClusterUpdate(const Upstream::Cluster& cluster) override {
    ASSERT_IS_MAIN_OR_TEST_THREAD();

    uint64_t total_weight = 0;
    uint64_t min_weight = std::numeric_limits<uint64_t>::max();
    // Walk the same hosts that DefaultEvenShareMonitor counts -- every priority, healthy or not --
    // so that a cluster whose hosts all carry the same weight yields the same share in either mode.
    for (const auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
      for (const auto& host : host_set->hosts()) {
        total_weight += host->weight();
        min_weight = std::min<uint64_t>(min_weight, host->weight());
      }
    }

    double new_share_factor = 1.0;
    if (total_weight != 0) {
      // Without a weight of our own, take the smallest weight in the cluster. That is no larger
      // than the share of any instance, so an instance which was never given its weight
      // under-claims the bucket instead of over-claiming it -- which an even `1 / N` share would do
      // for every instance whose weight is below the cluster average, the very skew that this mode
      // exists to correct.
      const uint64_t weight = self_weight_.value_or(min_weight);
      // A `self_weight` left behind by an earlier, larger topology can exceed the current total.
      // Cap at the whole bucket, which is all that an instance alone in its cluster gets anyway.
      new_share_factor =
          std::min(1.0, static_cast<double>(weight) / static_cast<double>(total_weight));
    }

    share_factor_.store(new_share_factor);
    return new_share_factor;
  }

private:
  const absl::optional<uint64_t> self_weight_;
  Stats::Gauge& self_weight_not_found_;
  std::atomic<double> share_factor_{1.0};
};

ShareProviderManager::ShareProviderManager(Event::Dispatcher& main_dispatcher,
                                           const Upstream::Cluster& cluster,
                                           const LocalInfo::LocalInfo& local_info,
                                           Stats::Scope& scope)
    : main_dispatcher_(main_dispatcher), cluster_(cluster), local_info_(local_info), scope_(scope),
      even_share_monitor_(std::make_shared<DefaultEvenShareMonitor>()) {
  // It's safe to capture the local cluster reference here because the local cluster is
  // guaranteed to be static cluster and should never be removed.
  handle_ = cluster_.prioritySet().addMemberUpdateCb([this](const auto&, const auto&) {
    even_share_monitor_->onLocalClusterUpdate(cluster_);
    if (weighted_share_monitor_ != nullptr) {
      weighted_share_monitor_->onLocalClusterUpdate(cluster_);
    }
  });
  // Prime the monitor with the current membership, since the callback above only fires on the next
  // change. The weighted monitor is primed where it is created instead, because it is created
  // lazily and so does not exist yet.
  even_share_monitor_->onLocalClusterUpdate(cluster_);
}

ShareProviderManager::~ShareProviderManager() {
  // Ensure the callback is unregistered on the main dispatcher thread.
  main_dispatcher_.post([h = std::move(handle_)]() {});
}

ShareProviderSharedPtr
ShareProviderManager::getShareProvider(const ProtoLocalClusterRateLimit& config) const {
  if (config.share_mode() != ProtoLocalClusterRateLimit::WEIGHTED) {
    return even_share_monitor_;
  }

  // Config load is main thread only, as is the membership update callback that reads this, so no
  // locking is needed here.
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (weighted_share_monitor_ == nullptr) {
    weighted_share_monitor_ = std::make_shared<WeightedShareMonitor>(local_info_, scope_);
    weighted_share_monitor_->onLocalClusterUpdate(cluster_);
  }
  return weighted_share_monitor_;
}

ShareProviderManagerSharedPtr
ShareProviderManager::singleton(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm,
                                Singleton::Manager& manager,
                                const LocalInfo::LocalInfo& local_info, Stats::Scope& scope) {
  return manager.getTyped<ShareProviderManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(local_ratelimit_share_provider_manager),
      [&dispatcher, &cm, &local_info, &scope]() -> Singleton::InstanceSharedPtr {
        const auto& local_cluster_name = cm.localClusterName();
        if (!local_cluster_name.has_value()) {
          return nullptr;
        }
        auto cluster = cm.getActiveOrWarmingCluster(local_cluster_name.value());
        if (!cluster.has_value()) {
          return nullptr;
        }
        return ShareProviderManagerSharedPtr{
            new ShareProviderManager(dispatcher, cluster.value().get(), local_info, scope)};
      });
}

RateLimitTokenBucket::RateLimitTokenBucket(uint64_t max_tokens, uint64_t tokens_per_fill,
                                           std::chrono::milliseconds fill_interval,
                                           TimeSource& time_source, bool shadow_mode)
    : token_bucket_(max_tokens, time_source,
                    // Calculate the fill rate in tokens per second.
                    tokens_per_fill / std::chrono::duration<double>(fill_interval).count()),
      fill_interval_(fill_interval), shadow_mode_(shadow_mode) {}
bool RateLimitTokenBucket::consume(double factor, uint64_t to_consume) {
  ASSERT(!(factor <= 0.0 || factor > 1.0));
  auto cb = [tokens = to_consume / factor](double total) { return total < tokens ? 0.0 : tokens; };
  return token_bucket_.consume(cb) != 0.0;
}

void RateLimitTokenBucket::refill(uint64_t tokens) {
  if (tokens == 0) {
    return;
  }
  // Use a negative consumed value to add tokens back, capped so we never exceed max_tokens.
  token_bucket_.consume([tokens_to_refill = static_cast<double>(tokens),
                         max = token_bucket_.maxTokens()](double total) -> double {
    const double headroom = max - total;
    if (headroom <= 0) {
      return 0.0; // Already at or above max, nothing to refill.
    }
    // Return negative consumed = tokens added back, capped at available headroom.
    return -std::min(tokens_to_refill, headroom);
  });
}

LocalRateLimiterImpl::LocalRateLimiterImpl(
    const std::chrono::milliseconds fill_interval, const uint64_t max_tokens,
    const uint64_t tokens_per_fill, Event::Dispatcher& dispatcher,
    const Protobuf::RepeatedPtrField<
        envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>& descriptors,
    bool always_consume_default_token_bucket, ShareProviderSharedPtr shared_provider,
    uint32_t lru_size)
    : time_source_(dispatcher.timeSource()), share_provider_(std::move(shared_provider)),
      always_consume_default_token_bucket_(always_consume_default_token_bucket) {
  // Ignore the default token bucket if fill_interval is 0 because 0 fill_interval means nothing
  // and has undefined behavior.
  if (fill_interval.count() > 0) {
    if (max_tokens == 0) {
      // max_tokens=0 means always reject; no token bucket needed.
      always_deny_default_ = true;
    } else {
      if (fill_interval < std::chrono::milliseconds(50)) {
        throw EnvoyException("local rate limit token bucket fill timer must be >= 50ms");
      }
      default_token_bucket_ = std::make_shared<RateLimitTokenBucket>(
          max_tokens, tokens_per_fill, fill_interval, time_source_, false);
    }
  }

  for (const auto& descriptor : descriptors) {
    RateLimit::LocalDescriptor new_descriptor;
    bool wildcard_found = false;
    new_descriptor.entries_.reserve(descriptor.entries_size());
    for (const auto& entry : descriptor.entries()) {
      if (entry.value().empty()) {
        wildcard_found = true;
      }
      new_descriptor.entries_.push_back({entry.key(), entry.value()});
    }

    const auto per_descriptor_max_tokens = descriptor.token_bucket().max_tokens();
    const auto per_descriptor_tokens_per_fill =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(descriptor.token_bucket(), tokens_per_fill, 1);
    const auto per_descriptor_fill_interval = std::chrono::milliseconds(
        PROTOBUF_GET_MS_OR_DEFAULT(descriptor.token_bucket(), fill_interval, 0));
    const auto shadow_mode = descriptor.shadow_mode();

    // Validate that the descriptor's fill interval is logically correct (same
    // constraint of >=50msec as for fill_interval). Skip the check when max_tokens=0
    // since the fill interval is irrelevant for an always-reject bucket.
    if (per_descriptor_max_tokens != 0 &&
        per_descriptor_fill_interval < std::chrono::milliseconds(50)) {
      throw EnvoyException("local rate limit descriptor token bucket fill timer must be >= 50ms");
    }

    if (wildcard_found) {
      DynamicDescriptorSharedPtr dynamic_descriptor = std::make_shared<DynamicDescriptor>(
          per_descriptor_max_tokens, per_descriptor_tokens_per_fill, per_descriptor_fill_interval,
          lru_size, dispatcher.timeSource(), shadow_mode);
      dynamic_descriptors_.addDescriptor(std::move(new_descriptor), std::move(dynamic_descriptor));
      continue;
    }
    RateLimitTokenBucketSharedPtr per_descriptor_token_bucket =
        std::make_shared<RateLimitTokenBucket>(
            per_descriptor_max_tokens, per_descriptor_tokens_per_fill, per_descriptor_fill_interval,
            time_source_, shadow_mode);
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
    if (match_result.request_descriptor.get().is_negative_hits_ &&
        match_result.request_descriptor.get().hits_addend_.has_value()) {
      // Negative addend means refill tokens instead of consuming.
      match_result.token_bucket->refill(match_result.request_descriptor.get().hits_addend_.value());
    } else if (!match_result.token_bucket->consume(
                   share_factor, match_result.request_descriptor.get().hits_addend_.value_or(1))) {
      // If the request is forbidden by a descriptor, return the result and the descriptor
      // token bucket.
      return {false, std::shared_ptr<TokenBucketContext>(match_result.token_bucket),
              match_result.request_descriptor.get().x_ratelimit_option_};
    }
    ENVOY_LOG(trace,
              "request allowed by descriptor with fill rate: {}, maxToken: {}, remainingToken {}",
              match_result.token_bucket->fillRate(), match_result.token_bucket->maxTokens(),
              match_result.token_bucket->remainingTokens());
  }

  // See if the request is forbidden by the default token bucket.
  if (matched_results.empty() || always_consume_default_token_bucket_) {
    if (default_token_bucket_ == nullptr) {
      if (always_deny_default_) {
        return {false, nullptr};
      }
      return {
          true,
          matched_results.empty()
              ? std::shared_ptr<TokenBucketContext>(nullptr)
              : std::shared_ptr<TokenBucketContext>(matched_results[0].token_bucket),
          matched_results.empty()
              ? RateLimit::XRateLimitOption::RateLimit_XRateLimitOption_UNSPECIFIED
              : matched_results[0].request_descriptor.get().x_ratelimit_option_,

      };
    }
    ASSERT(default_token_bucket_ != nullptr);

    if (const bool result = default_token_bucket_->consume(share_factor); !result) {
      // If the request is forbidden by the default token bucket, return the result and the
      // default token bucket.
      return {false, std::shared_ptr<TokenBucketContext>(default_token_bucket_),
              RateLimit::XRateLimitOption::RateLimit_XRateLimitOption_UNSPECIFIED};
    }

    // If the request is allowed then return the result the token bucket. The descriptor
    // token bucket will be selected as priority if it exists.
    return {true, matched_results.empty() ? default_token_bucket_ : matched_results[0].token_bucket,
            matched_results.empty()
                ? RateLimit::XRateLimitOption::RateLimit_XRateLimitOption_UNSPECIFIED
                : matched_results[0].request_descriptor.get().x_ratelimit_option_};
  };

  ASSERT(!matched_results.empty());
  std::shared_ptr<TokenBucketContext> bucket_context =
      std::shared_ptr<TokenBucketContext>(matched_results[0].token_bucket);
  return {true, bucket_context, matched_results[0].request_descriptor.get().x_ratelimit_option_};
}

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
