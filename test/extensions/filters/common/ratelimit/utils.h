#pragma once

#include <vector>

#include "envoy/service/ratelimit/v3/rls.pb.h"

namespace Envoy {
namespace RateLimit {

inline envoy::service::ratelimit::v3::RateLimitResponse_DescriptorStatus
buildDescriptorStatus(uint32_t requests_per_unit,
                      envoy::service::ratelimit::v3::RateLimitResponse_RateLimit_Unit unit,
                      std::string name, uint32_t limit_remaining, uint32_t seconds_until_reset) {
  envoy::service::ratelimit::v3::RateLimitResponse_DescriptorStatus statusMsg;
  statusMsg.set_limit_remaining(limit_remaining);
  statusMsg.mutable_duration_until_reset()->set_seconds(seconds_until_reset);
  if (requests_per_unit) {
    envoy::service::ratelimit::v3::RateLimitResponse_RateLimit* limitMsg =
        statusMsg.mutable_current_limit();
    limitMsg->set_requests_per_unit(requests_per_unit);
    limitMsg->set_unit(unit);
    limitMsg->set_name(name);
  }
  return statusMsg;
}

} // namespace RateLimit
} // namespace Envoy
