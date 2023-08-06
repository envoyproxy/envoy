#pragma once
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/matcher.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse;
using QuotaAssignmentAction = ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse::
    BucketAction::QuotaAssignmentAction;

/**
 * Possible async results for a limit call.
 */
enum class RateLimitStatus {
  // The request is not over limit.
  OK,
  // The request is over limit.
  OverLimit,
  // The rate limit service could not be queried.
  Error,
};

class RateLimitQuotaFilter : public Http::PassThroughFilter,
                             public RateLimitQuotaCallbacks,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  RateLimitQuotaFilter(FilterConfigConstSharedPtr config,
                       Server::Configuration::FactoryContext& factory_context,
                       BucketsContainer& quota_buckets,
                       RateLimitQuotaUsageReports& quota_usage_reports, ThreadLocalClient& client)
      : config_(std::move(config)), factory_context_(factory_context),
        quota_buckets_(quota_buckets), quota_usage_reports_(quota_usage_reports), client_(client) {
    createMatcher();
  }

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  void onDestroy() override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // RateLimitQuota::RateLimitQuotaCallbacks
  void onQuotaResponse(RateLimitQuotaResponse& response) override;

  // Perform request matching. It returns the generated bucket ids if the matching succeeded,
  // error status otherwise.
  absl::StatusOr<Matcher::ActionPtr> requestMatching(const Http::RequestHeaderMap& headers);

  Http::Matching::HttpMatchingDataImpl matchingData() {
    ASSERT(data_ptr_ != nullptr);
    return *data_ptr_;
  }

  ~RateLimitQuotaFilter() override {
    // Notify the client that has been destroyed and its callback which is pointer to this pointer
    // can not be used anymore.
    if (client_.rate_limit_client != nullptr) {
      client_.rate_limit_client->resetCallback();
    }
  }

private:
  // Create the matcher factory and matcher.
  void createMatcher();
  // Create new bucket element.
  Http::FilterHeadersStatus
  createNewBucketAndSendReport(const BucketId& bucket_id,
                               const RateLimitOnMatchAction& match_action);

  FilterConfigConstSharedPtr config_;
  Server::Configuration::FactoryContext& factory_context_;
  Http::StreamDecoderFilterCallbacks* callbacks_ = nullptr;
  RateLimitQuotaValidationVisitor visitor_ = {};
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher_ = nullptr;
  std::unique_ptr<Http::Matching::HttpMatchingDataImpl> data_ptr_ = nullptr;

  // Don't take ownership here and these objects are stored in TLS.
  BucketsContainer& quota_buckets_;
  RateLimitQuotaUsageReports& quota_usage_reports_;
  ThreadLocalClient& client_;

  bool initiating_call_{};
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
