#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

/**
 * Well-known http filter names.
 * NOTE: New filters should use the well known name: envoy.filters.thrift.name.
 */
class ThriftFilterNameValues {
public:
  // Ratelimit filter
  const std::string RATE_LIMIT = "envoy.filters.thrift.rate_limit";

  // Router filter
  const std::string ROUTER = "envoy.filters.thrift.router";
};

using ThriftFilterNames = ConstSingleton<ThriftFilterNameValues>;

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
