#pragma once

#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

class RateLimitQuotaCallbacks;

class MockRateLimitQuotaCallbacks : public RateLimitQuotaCallbacks {
public:
  MockRateLimitQuotaCallbacks() = default;
  ~MockRateLimitQuotaCallbacks() override = default;

  MOCK_METHOD(void, onQuotaResponse,
              (envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse & response));
};

class MockRateLimitClient : public RateLimitClient {
public:
  MockRateLimitClient() = default;
  ~MockRateLimitClient() override = default;

  MOCK_METHOD(absl::Status, startStream, (const StreamInfo::StreamInfo*));
  MOCK_METHOD(void, closeStream, ());
  MOCK_METHOD(void, sendUsageReport, (absl::optional<size_t>));

  MOCK_METHOD(void, setCallback, (RateLimitQuotaCallbacks*));
  MOCK_METHOD(void, resetCallback, ());
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
