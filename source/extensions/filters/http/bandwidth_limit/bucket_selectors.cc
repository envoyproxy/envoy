#include "source/extensions/filters/http/bandwidth_limit/bucket_selectors.h"

#include "source/extensions/filters/http/common/stream_rate_limiter.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

using Envoy::Extensions::HttpFilters::Common::StreamRateLimiter;

SINGLETON_MANAGER_REGISTRATION(bandwidth_limiter_named_bucket_singleton);

std::shared_ptr<NamedBucketSingleton>
NamedBucketSingleton::get(Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<NamedBucketSingleton>(
      SINGLETON_MANAGER_REGISTERED_NAME(bandwidth_limiter_named_bucket_singleton),
      [] { return std::make_shared<NamedBucketSingleton>(); });
}

BucketAndStats::BucketAndStats(absl::string_view name, TimeSource& time_source, Stats::Scope& scope,
                               uint64_t limit_kbps, std::chrono::milliseconds fill_interval)
    // The token bucket is configured with a max token count of the number of
    // bytes per second, and refills at the same rate, so that we have a per
    // second limit which refills gradually in 1/fill_interval increments.
    : bucket_(std::make_shared<SharedTokenBucketImpl>(
          StreamRateLimiter::kiloBytesToBytes(limit_kbps), time_source,
          StreamRateLimiter::kiloBytesToBytes(limit_kbps))),
      stats_(generateStats(name, scope)), fill_interval_(fill_interval) {}

std::string ClientCnBucketSelector::bucketName(const StreamInfo::StreamInfo& stream_info) const {
  auto ssl = stream_info.downstreamAddressProvider().sslConnection();
  if (!ssl || ssl->subjectPeerCertificate().empty()) {
    return default_bucket_name_;
  }
  return absl::StrReplaceAll(name_template_, {{"{CN}", ssl->subjectPeerCertificate()}});
}

OptRef<const BucketAndStats>
NamedBucketSelector::getBucket(const StreamInfo::StreamInfo& stream_info) {
  return singleton_->getBucket(bucketName(stream_info), create_bucket_);
}

absl::Status NamedBucketSingleton::setBucket(absl::string_view name,
                                             std::unique_ptr<BucketAndStats> bucket) {
  absl::MutexLock lock(mu_);
  auto result = buckets_.try_emplace(name, std::move(bucket));
  if (result.second) {
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError("duplicate bucket name in named_bucket_configurations");
}

OptRef<BucketAndStats> NamedBucketSingleton::getBucket(absl::string_view name,
                                                       BucketCreationFn& create_bucket) {
  absl::MutexLock lock(mu_);
  if (create_bucket == nullptr) {
    auto it = buckets_.find(name);
    if (it == buckets_.end()) {
      return absl::nullopt;
    }
    return *it->second;
  }
  auto& ptr = buckets_[name];
  if (!ptr) {
    ptr = create_bucket(name);
  }
  return *ptr;
}

ClientCnBucketSelector::ClientCnBucketSelector(std::shared_ptr<NamedBucketSingleton> singleton,
                                               BucketCreationFn bucket_creation_fn,
                                               absl::string_view default_bucket_name,
                                               absl::string_view name_template)
    : NamedBucketSelector(std::move(singleton), std::move(bucket_creation_fn)),
      default_bucket_name_(default_bucket_name),
      name_template_(name_template.empty() ? "{CN}" : name_template) {}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
