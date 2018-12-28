#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace DubboFilters {

/**
 * Well-known http filter names.
 * NOTE: New filters should use the well known name: envoy.filters.dubbo.name.
 */
class DubboFilterNameValues {
public:
  // Router filter
  const std::string ROUTER = "envoy.filters.dubbo.router";
};

typedef ConstSingleton<DubboFilterNameValues> DubboFilterNames;

} // namespace DubboFilters
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
