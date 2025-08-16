#pragma once

#include "source/extensions/filters/common/ratelimit/ratelimit.h"
#include <cstdint>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {
class XRateLimitHeaderUtils {
public:
  static Http::ResponseHeaderMapPtr
  create(Filters::Common::RateLimit::DescriptorStatusListPtr&& descriptor_statuses);

private:
  static uint32_t
  convertRateLimitUnit(envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::Unit unit, uint32_t unit_multiplier);
};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
