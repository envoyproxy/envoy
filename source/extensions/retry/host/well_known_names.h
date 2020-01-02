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
  const std::string OmitCanaryHostsPredicate = "envoy.retry_host_predicates.omit_canary_hosts";
  const std::string OmitHostsPredicate = "envoy.retry_host_predicates.omit_hosts";
};

/**
 * Well-known metadata filter namespaces.
 */
class MetadataFilterValues {
public:
  // Filter namespace for built-in load balancer.
  const std::string ENVOY_LB = "envoy.lb";
};

using MetadataFilters = ConstSingleton<MetadataFilterValues>;

using RetryHostPredicateValues = ConstSingleton<RetryHostPredicatesNameValues>;

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
