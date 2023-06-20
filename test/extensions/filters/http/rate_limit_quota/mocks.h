#pragma once

#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/extensions/filters/http/rate_limit_quota/client.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

class MockRateLimitQuotaCallbacks : public RateLimitQuotaCallbacks {
public:
  MockRateLimitQuotaCallbacks() = default;
  ~MockRateLimitQuotaCallbacks() override = default;

  MOCK_METHOD(void, onQuotaResponse,
              (envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse & response));
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
