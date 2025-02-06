#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

enum class DrainDirection {
  /**
   * Not draining yet. Default value, should not be externally set.
   */
  None = 0,

  /**
   * Drain inbound connections only.
   */
  InboundOnly,

  /**
   * Drain both inbound and outbound connections.
   */
  All,
};

class DrainDecision {
public:
  virtual ~DrainDecision() = default;

  /**
   * @return TRUE if a connection should be drained and closed. It is up to individual network
   *         filters to determine when this should be called for the least impact possible.
   * @param direction supplies the direction for which the caller is checking drain close.
   */
  virtual bool drainClose(DrainDirection scope) const PURE;
};

} // namespace Network
} // namespace Envoy
