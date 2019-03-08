#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

/**
 * Well-known retry host predicate names.
 */
class RetryHostPredicatesNameValues {
public:
  // Previous host predicate. Rejects hosts that have already been tried.
  const std::string PreviousHostsPredicate = "envoy.retry_host_predicates.previous_hosts";
};

typedef ConstSingleton<RetryHostPredicatesNameValues> RetryHostPredicateValues;

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
