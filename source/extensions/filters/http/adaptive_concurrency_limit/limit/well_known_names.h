#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrencyLimit {
namespace Limit {

/**
 * Well-known limit names.
 * NOTE: New limit implementations should use the well known name:
 * envoy.filters.http.adaptive_concurrency_limit.limit.name.
 */
class NameValues {
public:
  const std::string GRADIENT = "envoy.filters.http.adaptive_concurrency_limit.limit.gradient";
};

typedef ConstSingleton<NameValues> Names;

} // namespace Limit
} // namespace AdaptiveConcurrencyLimit
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy