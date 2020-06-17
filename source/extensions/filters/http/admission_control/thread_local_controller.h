#pragma once

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/http/codes.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

/*
 * Thread-local admission controller interface.
 */
class ThreadLocalController {
public:
  virtual ~ThreadLocalController() = default;

  // Record success/failure of a request and update the internal state of the controller to reflect
  // this.
  virtual void recordSuccess() PURE;
  virtual void recordFailure() PURE;

  // Returns the current number of recorded requests.
  virtual uint32_t requestTotalCount() PURE;

  // Returns the current number of recorded request successes.
  virtual uint32_t requestSuccessCount() PURE;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
