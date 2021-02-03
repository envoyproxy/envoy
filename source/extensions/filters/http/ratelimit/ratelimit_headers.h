#pragma once

#include "envoy/http/header_map.h"

#include "common/singleton/const_singleton.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

class XRateLimitHeaderValues {
public:
  const Http::LowerCaseString XRateLimitLimit{"x-ratelimit-limit"};
  const Http::LowerCaseString XRateLimitRemaining{"x-ratelimit-remaining"};
  const Http::LowerCaseString XRateLimitReset{"x-ratelimit-reset"};

  struct {
    const std::string Window{"w"};
    const std::string Name{"name"};
  } QuotaPolicyKeys;
};
using XRateLimitHeaders = ConstSingleton<XRateLimitHeaderValues>;

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
