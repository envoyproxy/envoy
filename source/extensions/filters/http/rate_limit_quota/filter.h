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

using FilterConfigSharedPtr = std::shared_ptr<
    const envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig>;

/**
 * TODO(tyxia) Placeholder!!!
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
  RateLimitQuotaFilter(FilterConfigSharedPtr config, Server::Configuration::FactoryContext&,
                       RateLimitClientPtr client)
      : config_(std::move(config)), rate_limit_client_(std::move(client)) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;

  ~RateLimitQuotaFilter() override = default;

private:
  FilterConfigSharedPtr config_;
  // TODO(tyxia) Rate limit client is a member of rate limit filter.
  RateLimitClientPtr rate_limit_client_;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
