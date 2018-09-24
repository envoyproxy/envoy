#pragma once

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

/**
 * Well-known retry priority load names.
 */
class RetryPriorityNameValues {
public:
  // Previous host predicate. Rejects hosts that have already been tried.
  const std::string PreviousPrioritiesRetryPriority = "envoy.retry_priorities.previous_priorities";
};

typedef ConstSingleton<RetryPriorityNameValues> RetryPriorityValues;

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
