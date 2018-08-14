#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Upstream {

/**
 * Limit calculates the concurrency limit based on response data.
 */
template <typename T> class Limit {
public:
  virtual ~Limit() {}

  /**
   * getLimit returns the currently estimated limit.
   * @return the current estimated concurrency limit for the cluster.
   */
  virtual uint32_t getLimit() PURE;

  /**
   * update is called when new data for a cluster is gathered. Updates the concurrency limit for
   * cluster based on the data provided.
   * @param data is the data to estimate to use to update the concurrency limit.
   */
  virtual void update(const T& data) PURE;
};

/**
 * Strategy enforces a calculated concurrency limit.
 */
template <typename T> class Strategy {
public:
  virtual ~Strategy() {}

  /**
   * canContinue determines if another concurrent request can be sent to the cluster.
   * @param context is additional context used in complex strategies.
   * @return True if an additional request is under the limit. False, if an additional request
   *         over the limit and thus has to be shed.
   */
  virtual bool canContinue(const T& context) PURE;
};

} // namespace Upstream
} // namespace Envoy
