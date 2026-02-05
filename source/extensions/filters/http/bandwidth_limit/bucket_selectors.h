#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/shared_token_bucket_impl.h"
#include "source/extensions/filters/http/bandwidth_limit/stats.h"

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

class BucketAndStats {
public:
  BucketAndStats(absl::string_view name, TimeSource& time_source, Stats::Scope& scope,
                 uint64_t limit_kbps, std::chrono::milliseconds fill_interval);
  std::shared_ptr<SharedTokenBucketImpl> bucket() const { return bucket_; }
  BandwidthLimitStats& stats() const { return stats_; }
  std::chrono::milliseconds fillInterval() const { return fill_interval_; }

private:
  std::shared_ptr<SharedTokenBucketImpl> bucket_;
  mutable BandwidthLimitStats stats_;
  std::chrono::milliseconds fill_interval_;
};

using BucketCreationFn = std::function<std::unique_ptr<BucketAndStats>(absl::string_view name)>;

class NamedBucketSingleton : public Singleton::Instance {
public:
  static std::shared_ptr<NamedBucketSingleton>
  get(Server::Configuration::ServerFactoryContext& context);
  absl::Status setBucket(absl::string_view name, std::unique_ptr<BucketAndStats> bucket);
  // Returns nullptr if the bucket does not exist and create_bucket is nullptr.
  OptRef<BucketAndStats> getBucket(absl::string_view name, BucketCreationFn& create_bucket);

private:
  absl::Mutex mu_;
  absl::flat_hash_map<std::string, std::unique_ptr<BucketAndStats>> buckets_ ABSL_GUARDED_BY(mu_);
};

class NamedBucketSelector {
public:
  NamedBucketSelector(std::shared_ptr<NamedBucketSingleton> singleton,
                      BucketCreationFn bucket_creation_fn)
      : singleton_(std::move(singleton)), create_bucket_(bucket_creation_fn) {}
  virtual ~NamedBucketSelector() = default;

  OptRef<const BucketAndStats> getBucket(const StreamInfo::StreamInfo& stream_info);
  virtual std::string bucketName(const StreamInfo::StreamInfo& stream_info) const PURE;

private:
  std::shared_ptr<NamedBucketSingleton> singleton_;
  // nullptr if create_bucket_if_not_existing is false.
  BucketCreationFn create_bucket_;
};

class FixedNamedBucketSelector : public NamedBucketSelector {
public:
  FixedNamedBucketSelector(std::shared_ptr<NamedBucketSingleton> singleton,
                           BucketCreationFn bucket_creation_fn, absl::string_view bucket_name)
      : NamedBucketSelector(std::move(singleton), std::move(bucket_creation_fn)),
        bucket_name_(bucket_name) {}

  std::string bucketName(const StreamInfo::StreamInfo&) const override { return bucket_name_; }

private:
  std::string bucket_name_;
};

class ClientCnBucketSelector : public NamedBucketSelector {
public:
  ClientCnBucketSelector(std::shared_ptr<NamedBucketSingleton> singleton,
                         BucketCreationFn bucket_creation_fn, absl::string_view default_bucket_name,
                         absl::string_view name_template);

  std::string bucketName(const StreamInfo::StreamInfo& stream_info) const override;

private:
  const std::string default_bucket_name_;
  const std::string name_template_;
};

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
