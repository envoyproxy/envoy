#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace SipFilters {

/**
 * Well-known http filter names.
 * NOTE: New filters should use the well known name: envoy.filters.sip.name.
 */
class SipFilterNameValues {
public:
  // Ratelimit filter
  const std::string RATE_LIMIT = "envoy.filters.sip.rate_limit";

  // Router filter
  const std::string ROUTER = "envoy.filters.sip.router";
};

using SipFilterNames = ConstSingleton<SipFilterNameValues>;

} // namespace SipFilters
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
