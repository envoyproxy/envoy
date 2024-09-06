#pragma once
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/grpc/async_client_manager.h"
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
#include "source/extensions/matching/input_matchers/cel_matcher/config.h"

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
                             public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  RateLimitQuotaFilter(FilterConfigConstSharedPtr config,
                       Server::Configuration::FactoryContext& factory_context,
                       BucketsCache& quota_buckets, ThreadLocalClient& client,
                       Grpc::GrpcServiceConfigWithHashKey config_with_hash_key)
      : config_(std::move(config)), config_with_hash_key_(config_with_hash_key),
        factory_context_(factory_context), quota_buckets_(quota_buckets), client_(client),
        time_source_(factory_context.serverFactoryContext().mainThreadDispatcher().timeSource()) {
    createMatcher();
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override;
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
    // Notify the client that the filter has been destroyed and the callback can not be used
    // anymore.
    if (client_.rate_limit_client != nullptr) {
      client_.rate_limit_client->resetCallback();
    }
  }

private:
  // Create the matcher factory and matcher.
  void createMatcher();
  // Create a new bucket and add it to the quota bucket cache.
  void createNewBucket(const BucketId& bucket_id, const RateLimitOnMatchAction& match_action,
                       size_t id);
  // Send the report to RLQS server immediately.
  Http::FilterHeadersStatus sendImmediateReport(const size_t bucket_id,
                                                const RateLimitOnMatchAction& match_action);

  Http::FilterHeadersStatus setUsageAndResponseFromAction(const BucketAction& action,
                                                          size_t bucket_id);

  Http::FilterHeadersStatus processCachedBucket(size_t bucket_id,
                                                const RateLimitOnMatchAction& match_action);
  // TODO(tyxia) Build the customized response based on `DenyResponseSettings`.
  // Send a deny response and update quota usage if provided.
  Http::FilterHeadersStatus sendDenyResponse(QuotaUsage* quota_usage = nullptr) {
    callbacks_->sendLocalReply(Envoy::Http::Code::TooManyRequests, "", nullptr, absl::nullopt, "");
    callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::RateLimited);
    if (quota_usage)
      quota_usage->num_requests_denied++;
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Send an allow response and update quota usage if provided.
  Http::FilterHeadersStatus sendAllowResponse(QuotaUsage* quota_usage = nullptr) {
    if (quota_usage)
      quota_usage->num_requests_allowed++;
    return Http::FilterHeadersStatus::Continue;
  }

  // Get the FilterHeadersStatus to return when a selected bucket has an expired
  // assignment. Note: this does not actually remove the expired entity from the
  // cache.
  Http::FilterHeadersStatus processExpiredBucket(size_t bucket_id,
                                                 const RateLimitOnMatchAction& match_action);

  FilterConfigConstSharedPtr config_;
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
  Server::Configuration::FactoryContext& factory_context_;
  Http::StreamDecoderFilterCallbacks* callbacks_ = nullptr;
  RateLimitQuotaValidationVisitor visitor_ = {};
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher_ = nullptr;
  std::unique_ptr<Http::Matching::HttpMatchingDataImpl> data_ptr_ = nullptr;

  // Reference to the objects that are stored in TLS.
  BucketsCache& quota_buckets_;
  ThreadLocalClient& client_;
  TimeSource& time_source_;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
