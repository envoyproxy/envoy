#pragma once

#include "envoy/http/header_map.h"

#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace RateLimit {

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
} // namespace RateLimit
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
