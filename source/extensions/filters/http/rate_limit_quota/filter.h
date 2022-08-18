#pragma once
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using FilterConfig =
    envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig;
using FilterConfigConstSharedPtr = std::shared_ptr<const FilterConfig>;

/**
 * TODO(tyxia) Placeholder!!! Implement as needed.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;
};

// TODO(tyxia) Starts with passThroughFilter consider streamFilter.
class RateLimitQuotaFilter : public Http::PassThroughFilter,
                             public RequestCallbacks,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  RateLimitQuotaFilter(FilterConfigConstSharedPtr config, Server::Configuration::FactoryContext&,
                       RateLimitClientPtr client)
      : config_(std::move(config)), rate_limit_client_(std::move(client)) {}

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  void onDestroy() override;

  ~RateLimitQuotaFilter() override = default;

private:
  FilterConfigConstSharedPtr config_;
  // TODO(tyxia) Rate limit client is a member of rate limit filter.
  RateLimitClientPtr rate_limit_client_;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
