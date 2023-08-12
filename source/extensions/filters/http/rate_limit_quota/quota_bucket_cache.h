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

#include "absl/time/time.h"

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

struct QuotaUsage {
  // Requests the data plane has allowed through.
  uint64_t num_requests_allowed;
  // Requests throttled.
  uint64_t num_requests_denied;
  std::chrono::nanoseconds last_report;
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
  // std::unique_ptr<BucketAction> bucket_action;
  BucketAction bucket_action;
  // TODO(tyxia) No need to store bucket ID  as it is already the key of BucketsContainer.
  // TODO(tyxia) Seems unused
  QuotaUsage quota_usage;
  // BucketQuotaUsage quota_usage;
};

// using BucketsContainer = absl::node_hash_map<BucketId, Bucket, BucketIdHash, BucketIdEqual>;
using BucketsContainer =
    absl::node_hash_map<BucketId, std::unique_ptr<Bucket>, BucketIdHash, BucketIdEqual>;

struct ThreadLocalClient : public Logger::Loggable<Logger::Id::rate_limit_quota> {
  ThreadLocalClient(Envoy::Event::Dispatcher& event_dispatcher) : dispatcher(event_dispatcher) {
    // Create the quota usage report method that sends the reports the RLS server periodically.
    send_reports_timer = dispatcher.createTimer([this] {
      if (rate_limit_client != nullptr) {
        rate_limit_client->sendUsageReport(absl::nullopt);
      } else {
        ENVOY_LOG(error, "Rate limit client has been destroyed; no periodical report send");
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
};

class ThreadLocalBucket : public Envoy::ThreadLocal::ThreadLocalObject {
public:
  // TODO(tyxia) Here is similar to defer initialization methodology.
  // We have the empty hash map in thread local storage at the beginning and build the map later
  // in the `rate_limit_quota_filter` when the request comes.
  ThreadLocalBucket(Envoy::Event::Dispatcher& dispatcher) : client_(dispatcher) {}

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
  QuotaBucketCache(Envoy::Server::Configuration::FactoryContext& context)
      : tls(context.threadLocal()) {
    tls.set([](Envoy::Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalBucket>(dispatcher);
    });
  }
  Envoy::ThreadLocal::TypedSlot<ThreadLocalBucket> tls;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
