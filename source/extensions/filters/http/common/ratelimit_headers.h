#pragma once

#include "envoy/http/header_map.h"

#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace RateLimit {

constexpr absl::string_view QuotaPolicyWindow = "w";
constexpr absl::string_view QuotaPolicyName = "name";

class XRateLimitHeaderValues {
public:
  const Http::LowerCaseString XRateLimitLimit{"x-ratelimit-limit"};
  const Http::LowerCaseString XRateLimitRemaining{"x-ratelimit-remaining"};
  const Http::LowerCaseString XRateLimitReset{"x-ratelimit-reset"};
};

using XRateLimitHeaders = ConstSingleton<XRateLimitHeaderValues>;

class RetryAfterHeaderValues {
public:
  const Http::LowerCaseString RetryAfter{"retry-after"};
};

using RetryAfterHeaders = ConstSingleton<RetryAfterHeaderValues>;
} // namespace RateLimit
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
