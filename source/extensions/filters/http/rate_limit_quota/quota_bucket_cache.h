#pragma once
#include <algorithm>
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::service::rate_limit_quota::v3::BucketId;
using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;

using BucketAction = ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse::BucketAction;
using BucketQuotaUsage =
    ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports::BucketQuotaUsage;

// Forward declaration
// class RateLimitClientImpl;

// Customized hash and equal struct for `BucketId` hash key.
struct BucketIdHash {
  size_t operator()(const BucketId& bucket_id) const { return MessageUtil::hash(bucket_id); }
};

struct BucketIdEqual {
  bool operator()(const BucketId& id1, const BucketId& id2) const {
    return Protobuf::util::MessageDifferencer::Equals(id1, id2);
  }
};

// Single bucket entry
struct Bucket {
  // Default constructor
  Bucket() = default;
  // Disable copy constructor and assignment.
  Bucket(const Bucket& bucket) = delete;
  Bucket& operator=(const Bucket& bucket) = delete;
  // Default move constructor and assignment.
  Bucket(Bucket&& bucket) = default;
  Bucket& operator=(Bucket&& bucket) = default;

  ~Bucket() {}

  // std::unique_ptr<RateLimitClient> rate_limit_client;
  // // The timer for sending the reports periodically.
  // Event::TimerPtr send_reports_timer;
  // Cached bucket action from the response that was received from the RLQS server.
  // BucketAction bucket_action;
  // TODO(tyxia) Thread local storage should take the ownership of all the objects so that
  // it is also responsible for destruction.
  std::unique_ptr<BucketAction> bucket_action;
  // TODO(tyxia) No need to store bucket ID  as it is already the key of BucketsContainer.
  // TODO(tyxia) Seems unused
  BucketQuotaUsage quota_usage;
};

// using BucketsContainer = absl::node_hash_map<BucketId, Bucket, BucketIdHash, BucketIdEqual>;
using BucketsContainer =
    absl::node_hash_map<BucketId, std::unique_ptr<Bucket>, BucketIdHash, BucketIdEqual>;

using FilterConfig =
    envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig;
using FilterConfigConstSharedPtr = std::shared_ptr<const FilterConfig>;

struct ThreadLocalClient {
  ThreadLocalClient(Envoy::Event::Dispatcher& event_dispatcher,
                    FilterConfigConstSharedPtr filter_config)
      : dispatcher(event_dispatcher), config(std::move(filter_config)) {
    send_reports_timer = dispatcher.createTimer([this] {
      if (rate_limit_client != nullptr) {
        rate_limit_client->sendUsageReport(config->domain(), absl::nullopt);
      } else {
        // TODO(tyxia) Change it to ENVOY BUG
        std::cout << "tyxia_null_rate_limit_client\n";
      }
    });
  }
  // TODO(tyxia) Different from Google3, look
  // // Disable copy constructor and assignment.
  // ThreadLocalClient(const ThreadLocalClient& client) = delete;
  // ThreadLocalClient& operator=(const ThreadLocalClient& client) = delete;
  // // Default move constructor and assignment.
  // ThreadLocalClient(ThreadLocalClient&& client) = default;
  // ThreadLocalClient& operator=(ThreadLocalClient&& client) = default;

  // TODO(tyxia) Memory issue probably because this never hit?? can try again now.
  ~ThreadLocalClient() {
    if (rate_limit_client != nullptr) {
      rate_limit_client->closeStream();
    }
  }

  // TODO(tyxia) Each bucket owns the unique grpc client for sending the quota usage report
  // periodically.
  // std::unique_ptr<RateLimitClientImpl> rate_limit_client;
  // TODO(tyxia) Store the abstract interface to avoid cyclic dependency between quota bucket and
  // client.
  std::unique_ptr<RateLimitClient> rate_limit_client;
  // The timer for sending the reports periodically.
  Event::TimerPtr send_reports_timer;

  // TODO(tyxia) Maybe no need to store dispatcher.
  Envoy::Event::Dispatcher& dispatcher;
  FilterConfigConstSharedPtr config;
};

class ThreadLocalBucket : public Envoy::ThreadLocal::ThreadLocalObject {
public:
  // TODO(tyxia) Here is similar to defer initialization methodology.
  // We have the empty hash map in thread local storage at the beginning and build the map later
  // in the `rate_limit_quota_filter` when the request comes.
  ThreadLocalBucket(Envoy::Event::Dispatcher& dispatcher, FilterConfigConstSharedPtr config)
      : client_(dispatcher, std::move(config)) {}

  // Return the buckets. Buckets are returned by reference so that caller site can modify its
  // content.
  BucketsContainer& quotaBuckets() { return quota_buckets_; }
  // Return the quota usage reports.
  RateLimitQuotaUsageReports& quotaUsageReports() { return quota_usage_reports_; }

  ThreadLocalClient& rateLimitClient() { return client_; }

private:
  // Thread local storage for bucket container and quota usage report.
  BucketsContainer quota_buckets_;
  RateLimitQuotaUsageReports quota_usage_reports_;
  ThreadLocalClient client_;
};

struct QuotaBucketCache {
  QuotaBucketCache(Envoy::Server::Configuration::FactoryContext& context,
                   FilterConfigConstSharedPtr config)
      : tls(context.threadLocal()) {
    tls.set([config = std::move(config)](Envoy::Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalBucket>(dispatcher, std::move(config));
    });
  }
  Envoy::ThreadLocal::TypedSlot<ThreadLocalBucket> tls;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
