#pragma once

#include <string>

#include "source/common/singleton/const_singleton.h"

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
  // Router filter
  const std::string ROUTER = "envoy.filters.sip.router";
  const std::string SipProxy = "envoy.filters.network.sip_proxy";
};

using SipFilterNames = ConstSingleton<SipFilterNameValues>;

} // namespace SipFilters

// const std::string SipProxy = "envoy.filters.network.sip_proxy";

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
