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
#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/matcher.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"
#include "source/extensions/matching/input_matchers/cel_matcher/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse;
using QuotaAssignmentAction = ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse::
    BucketAction::QuotaAssignmentAction;
using FilterConfig =
    envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig;
using FilterConfigConstSharedPtr = std::shared_ptr<const FilterConfig>;
using DenyResponseSettings = envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings::DenyResponseSettings;

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
                             public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  RateLimitQuotaFilter(FilterConfigConstSharedPtr config,
                       Server::Configuration::FactoryContext& factory_context,
                       std::unique_ptr<RateLimitClient> local_client,
                       Grpc::GrpcServiceConfigWithHashKey config_with_hash_key,
                       Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher)
      : config_(std::move(config)), config_with_hash_key_(config_with_hash_key),
        factory_context_(factory_context), matcher_(matcher), client_(std::move(local_client)),
        time_source_(factory_context.serverFactoryContext().mainThreadDispatcher().timeSource()) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override;
  void onDestroy() override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // Perform request matching. It returns the generated bucket ids if the
  // matching succeeded, error status otherwise.
  absl::StatusOr<Matcher::ActionConstSharedPtr>
  requestMatching(const Http::RequestHeaderMap& headers);

  Http::Matching::HttpMatchingDataImpl matchingData() {
    ASSERT(data_ptr_ != nullptr);
    return *data_ptr_;
  }

private:
  Http::FilterHeadersStatus processCachedBucket(const DenyResponseSettings& deny_response_settings,
                                                CachedBucket& cached_bucket);
  bool shouldAllowRequest(const CachedBucket& cached_bucket);
  // Handle the first Matcher that's marked with keep_matching as a preview.
  void handlePreviewMatch(const Matcher::ActionConstSharedPtr& skipped_action);
  // Record the usage of a bucket, including bucket creation if hitting a new bucket.
  Http::FilterHeadersStatus recordBucketUsage(const Matcher::ActionConstSharedPtr& matched,
                                              bool is_preview_match);

  FilterConfigConstSharedPtr config_;
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
  Server::Configuration::FactoryContext& factory_context_;
  Http::StreamDecoderFilterCallbacks* callbacks_ = nullptr;
  RateLimitQuotaValidationVisitor visitor_ = {};
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher_;
  std::unique_ptr<Http::Matching::HttpMatchingDataImpl> data_ptr_ = nullptr;

  // Flipped false after hitting the first preview-mode Matcher. Future preview-mode matcher hits
  // shouldn't be recorded.
  bool first_skipped_match_ = true;

  // Own a local, filter-specific client to provider functions needed by worker
  // threads.
  std::unique_ptr<RateLimitClient> client_;
  TimeSource& time_source_;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
