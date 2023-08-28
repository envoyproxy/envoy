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
#include "source/extensions/matching/input_matchers/cel_matcher/config.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings;
using FilterConfig =
    envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig;
using FilterConfigConstSharedPtr = std::shared_ptr<const FilterConfig>;
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
                       Server::Configuration::FactoryContext& factory_context)
      : config_(std::move(config)), factory_context_(factory_context) {
    createMatcher();
  }

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  void onDestroy() override {}
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // RateLimitQuota::RateLimitQuotaCallbacks
  void onQuotaResponse(envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse&) override {}

  // Perform request matching. It returns the generated bucket ids if the matching succeeded,
  // error status otherwise.
  absl::StatusOr<Matcher::ActionPtr> requestMatching(const Http::RequestHeaderMap& headers);

  Http::Matching::HttpMatchingDataImpl matchingData() {
    ASSERT(data_ptr_ != nullptr);
    return *data_ptr_;
  }

  ~RateLimitQuotaFilter() override = default;

private:
  // Create the matcher factory and matcher.
  void createMatcher();

  FilterConfigConstSharedPtr config_;
  Server::Configuration::FactoryContext& factory_context_;
  Http::StreamDecoderFilterCallbacks* callbacks_ = nullptr;
  RateLimitQuotaValidationVisitor visitor_ = {};
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher_ = nullptr;
  std::unique_ptr<Http::Matching::HttpMatchingDataImpl> data_ptr_ = nullptr;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
