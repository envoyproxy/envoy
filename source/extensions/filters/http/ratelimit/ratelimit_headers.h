#pragma once

#include "source/extensions/filters/common/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {
class XRateLimitHeaderUtils {
public:
  static void populateHeaders(const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                              bool enabled,
                              const Filters::Common::RateLimit::DescriptorStatusList& statuses,
                              Http::ResponseHeaderMap& headers);

  static uint32_t
  convertRateLimitUnit(envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::Unit unit);
};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
