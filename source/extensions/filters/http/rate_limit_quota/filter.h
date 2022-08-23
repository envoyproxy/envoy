#pragma once
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"
// Needed for bucket id, temporary.
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using FilterConfig =
    envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig;
using FilterConfigConstSharedPtr = std::shared_ptr<const FilterConfig>;
using BucketSettings = envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings;

/**
 * TODO(tyxia) Placeholder!!! Implement as needed.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;
};

class RateLimitQuotaValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

using BucketId = envoy::service::rate_limit_quota::v3::BucketId;
// TODO(tyxia) Starts with passThroughFilter consider streamFilter.
class RateLimitQuotaFilter : public Http::PassThroughFilter,
                             public RequestCallbacks,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  RateLimitQuotaFilter(FilterConfigConstSharedPtr config,
                       Server::Configuration::FactoryContext& factory_context,
                       RateLimitClientPtr client)
      : config_(std::move(config)), rate_limit_client_(std::move(client)),
        factory_context_(factory_context) {}

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  void onDestroy() override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // TODO(tyxia) temporaliy put it here, once we get the quotabucketseeting from filter config we
  // don't need it.
  BucketId buildBucketsId(
      const Http::RequestHeaderMap&,
      const envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings&
          settings);

  std::vector<BucketSettings> buildMatcher();
  ~RateLimitQuotaFilter() override = default;

private:
  FilterConfigConstSharedPtr config_;
  // TODO(tyxia) Rate limit client is a member of rate limit filter.
  RateLimitClientPtr rate_limit_client_;
  Server::Configuration::FactoryContext& factory_context_;
  Http::StreamDecoderFilterCallbacks* callbacks_;
  RateLimitQuotaValidationVisitor visitor_ = {};
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher_;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
