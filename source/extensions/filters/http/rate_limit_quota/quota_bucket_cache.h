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
  // Last report time.
  std::chrono::nanoseconds last_report;
};

// This object stores the data for single bucket entry.
struct Bucket {
  // Default constructor
  Bucket() = default;
  // Disable copy constructor and assignment.
  Bucket(const Bucket& bucket) = delete;
  Bucket& operator=(const Bucket& bucket) = delete;
  // Default move constructor and assignment.
  Bucket(Bucket&& bucket) = default;
  Bucket& operator=(Bucket&& bucket) = default;

  ~Bucket() = default;

  // Cached action from the response that was received from the RLQS server.
  BucketAction bucket_action;
  // Cache quota usage.
  QuotaUsage quota_usage;
};

using BucketsCache =
    absl::node_hash_map<BucketId, std::unique_ptr<Bucket>, BucketIdHash, BucketIdEqual>;

struct ThreadLocalClient : public Logger::Loggable<Logger::Id::rate_limit_quota> {
  ThreadLocalClient(Envoy::Event::Dispatcher& dispatcher) {
    // Create the quota usage report method that sends the reports the RLS server periodically.
    send_reports_timer = dispatcher.createTimer([this] {
      if (rate_limit_client != nullptr) {
        rate_limit_client->sendUsageReport(absl::nullopt);
      } else {
        ENVOY_LOG(error, "Rate limit client has been destroyed; no periodical report send");
      }
    });
  }

  // Disable copy constructor and assignment.
  ThreadLocalClient(const ThreadLocalClient& client) = delete;
  ThreadLocalClient& operator=(const ThreadLocalClient& client) = delete;
  // Default move constructor and assignment.
  ThreadLocalClient(ThreadLocalClient&& client) = default;
  ThreadLocalClient& operator=(ThreadLocalClient&& client) = default;

  ~ThreadLocalClient() {
    if (rate_limit_client != nullptr) {
      // Close the stream.
      rate_limit_client->closeStream();
    }
  }

  // Rate limit client. It is owned here (in TLS) and is used by all the buckets.
  std::unique_ptr<RateLimitClient> rate_limit_client;
  // The timer for sending the reports periodically.
  Event::TimerPtr send_reports_timer;
};

class ThreadLocalBucket : public Envoy::ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalBucket(Envoy::Event::Dispatcher& dispatcher) : client_(dispatcher) {}

  // Return the buckets; returned by reference it can be modified.
  BucketsCache& quotaBuckets() { return quota_buckets_; }

  // Return the rate limit client; no ownership transferred.
  ThreadLocalClient& rateLimitClient() { return client_; }

private:
  // Thread local storage for bucket container and client.
  BucketsCache quota_buckets_;
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
