#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

class DrainDecision {
public:
  virtual ~DrainDecision() = default;

  /**
   * @return TRUE if a connection should be drained and closed. It is up to individual network
   *         filters to determine when this should be called for the least impact possible.
   */
  virtual bool drainClose() const PURE;
};

class PartitionedDrainDecision {
public:
  virtual ~PartitionedDrainDecision() = default;

  /**
   * @return TRUE if a connection should be drained and closed. It is up to individual network
   *         filters to determine when this should be called for the least impact possible.
   */
  virtual bool drainClose(uint64_t partition_id) const PURE;
};

} // namespace Network
} // namespace Envoy
