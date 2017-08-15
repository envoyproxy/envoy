#pragma once

#include <memory>

namespace Envoy {
namespace Upstream {

/**
 * A sink for "passive" health check events that might happen on every thread. For example, if a
 * special HTTP header is received, the data plane may decide to fast fail a host to avoid waiting
 * for the full HC interval to elapse before determining the host is active HC failed.
 */
class HealthCheckerSink {
public:
  virtual ~HealthCheckerSink() {}

  /**
   * Mark the sink as unhealthy. Note that this may not be immediate as events may need to be
   * propagated between multiple threads.
   */
  virtual void setUnhealthy() PURE;
};

typedef std::unique_ptr<HealthCheckerSink> HealthCheckerSinkPtr;

} // namespace Upstream
} // namespace Envoy
