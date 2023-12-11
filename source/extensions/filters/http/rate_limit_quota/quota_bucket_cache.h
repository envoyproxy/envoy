#pragma once
#include <algorithm>
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"

#include "source/common/common/token_bucket_impl.h"
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

struct QuotaUsage {
  // Requests allowed.
  uint64_t num_requests_allowed;
  // Requests throttled.
  uint64_t num_requests_denied;
  // Last report time.
  std::chrono::nanoseconds last_report;
};

// This object stores the data for single bucket entry.
struct Bucket {
  // BucketId object that is associated with this bucket. It is part of the report that is sent to
  // RLQS server.
  BucketId bucket_id;
  // Cached action from the response that was received from the RLQS server.
  BucketAction bucket_action;
  // Cache quota usage.
  QuotaUsage quota_usage;
  // Rate limiter based on token bucket algorithm.
  TokenBucketPtr token_bucket_limiter;
};

using BucketsCache = absl::flat_hash_map<size_t, std::unique_ptr<Bucket>>;

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

  // Return the buckets; returned by reference so it can be modified.
  BucketsCache& quotaBuckets() { return quota_buckets_; }

  // Return the rate limit client; returned by reference so no ownership transferred.
  ThreadLocalClient& rateLimitClient() { return client_; }

private:
  // Thread local storage for bucket container and client.
  BucketsCache quota_buckets_;
  ThreadLocalClient client_;
};

struct QuotaBucket {
  QuotaBucket(Envoy::Server::Configuration::FactoryContext& context)
      : tls(context.serverFactoryContext().threadLocal()) {
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
