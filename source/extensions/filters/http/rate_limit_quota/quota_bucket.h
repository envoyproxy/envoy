#pragma once
#include <algorithm>
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"
#include "source/common/protobuf/utility.h"

#include "source/extensions/filters/http/common/factory_base.h"

// #include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::service::rate_limit_quota::v3::BucketId;
// using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;
using BucketAction = ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse::BucketAction;
using QuotaAssignmentAction = ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse::
    BucketAction::QuotaAssignmentAction;
using BucketQuotaUsage =
    ::envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports::BucketQuotaUsage;

class RateLimitClientImpl;

// Customized hash and equal struct for `BucketId` hash key.
struct BucketIdHash {
  size_t operator()(const BucketId& bucket_id) const { return MessageUtil::hash(bucket_id); }
};

struct BucketIdEqual {
  bool operator()(const BucketId& id1, const BucketId& id2) const {
    return Protobuf::util::MessageDifferencer::Equals(id1, id2);
  }
};

struct UsageReport {
  uint64_t num_requests_allowed;
  uint64_t num_requests_denied;
  uint64_t time_elapsed_sec;
};

struct BucketElement {
  // Default constructor
  BucketElement() = default;

  // TODO(tyxia) This copy constructor is very tricky
  // the unique ptr inside of this class and can only be moveable.
  // BucketElement (const BucketElement& elem) = default;

  // Default move constructor and assignment.
  BucketElement(BucketElement&& elem) = default;
  BucketElement& operator=(BucketElement&& elem) = default;

  // TODO(tyxia) Each bucket owns the unique grpc client for sending the quota usage report
  // periodically.
  // TODO(tyxia) Use forward declaration for now.
  std::unique_ptr<RateLimitClientImpl> rate_limit_client_;
  BucketAction bucket_action;
  // TODO(tyxia) Should store bucket action intstead of assignment action
  // because abondon
  // QuotaAssignmentAction assignment;
  Event::TimerPtr send_reports_timer;
  UsageReport usage;
  // TODO(tyxia) Also stroes bucketId in its element.
  BucketId id;
  BucketQuotaUsage quota_usage;
};

struct Bucket {
  std::vector<BucketElement> bucket;
  // absl::node_hash_map<Http::RequestHeaderMap, BucketElement> bucket_map;
  BucketId id;
  Event::TimerPtr send_reports_timer;
};


struct BucketQuotaUsageInfo {
  // The index of `bucket_quota_usage` in the `RateLimitQuotaUsageReports`.
  int idx;
  std::string domain;
  BucketQuotaUsage usage;
};
using BucketContainer = absl::node_hash_map<BucketId, BucketElement, BucketIdHash, BucketIdEqual>;
using QuotaUsageContainer =
    absl::node_hash_map<BucketId, BucketQuotaUsageInfo, BucketIdHash, BucketIdEqual>;
using UsageReportsContainer =
    absl::node_hash_map<BucketId, BucketQuotaUsageInfo, BucketIdHash, BucketIdEqual>;

// TODO (tyxia) Maybe i can test this thread local cache along with token cache.
// TODO(tyxia) Thread local storage for all the necessary elements for the bucket.
// Currently it is actually a wrapper around node_hash_map.
class ThreadLocalBucket : public Envoy::ThreadLocal::ThreadLocalObject {
public:
  // TODO(tyxia) Here is similar to defer initialization methodology.
  // We have the empty hash map in thread local storage at the beginning and build the map later
  // in the `rate_limit_quota_filter` when the request comes.
  ThreadLocalBucket() = default;

  // Return the bucket by reference so that it can be modifed on the caller site.
  BucketContainer& buckets() { return buckets_; }

private:
  BucketContainer buckets_;
  QuotaUsageContainer quota_usage_;
  UsageReportsContainer usage_reports_;
};

struct BucketCache {
  BucketCache(Envoy::Server::Configuration::FactoryContext& context) : tls(context.threadLocal()) {
    tls.set([](Envoy::Event::Dispatcher&) {
      // Unused
      // dispatcher.timeSource()
      return std::make_shared<ThreadLocalBucket>();
    });
  }
  Envoy::ThreadLocal::TypedSlot<ThreadLocalBucket> tls;
};

// class ThreadLocalQuotaAssignment : public Envoy::ThreadLocal::ThreadLocalObject {
// public:
//   ThreadLocalQuotaAssignment() = default;

// private:
//   absl::node_hash_map<BucketId, QuotaAssignmentAction, BucketIdHash, BucketIdEqual>
//       quota_assignment_;
// };

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
