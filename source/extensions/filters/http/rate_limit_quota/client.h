#pragma once

#include <memory>

#include "envoy/common/pure.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using RateLimitQuotaResponsePtr =
    std::unique_ptr<envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse>;
using FilterConfig =
    envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig;
using FilterConfigConstSharedPtr = std::shared_ptr<const FilterConfig>;

/**
 * The callbacks used to communicate between RLQS filter and client.
 */
class RateLimitQuotaCallbacks {
public:
  virtual ~RateLimitQuotaCallbacks() = default;

  virtual void
  onQuotaResponse(envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse& response) PURE;
};

/**
 * A client used to query the rate limit quota service (RLQS).
 */
class RateLimitClient {
public:
  virtual ~RateLimitClient() = default;

  virtual absl::Status startStream(const StreamInfo::StreamInfo* stream_info) PURE;
  virtual void closeStream() PURE;
  virtual void sendUsageReport(absl::optional<size_t> bucket_id) PURE;

  virtual void setCallback(RateLimitQuotaCallbacks* callbacks) PURE;
  virtual void resetCallback() PURE;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
