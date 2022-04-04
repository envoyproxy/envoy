#pragma once

#include "source/extensions/filters/common/ratelimit/ratelimit.h"

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
  convertRateLimitUnit(envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::Unit unit);
};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
