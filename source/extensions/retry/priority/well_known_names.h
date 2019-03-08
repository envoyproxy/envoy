#pragma once

#include <string>

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
  // Previous priority retry priority. Excludes previously attempted priorities during retries.
  const std::string PreviousPrioritiesRetryPriority = "envoy.retry_priorities.previous_priorities";
};

typedef ConstSingleton<RetryPriorityNameValues> RetryPriorityValues;

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
