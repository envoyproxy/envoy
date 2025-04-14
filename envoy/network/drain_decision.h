#pragma once

#include <chrono>
#include <functional>

#include "envoy/common/callback.h"
#include "envoy/common/pure.h"

#include "absl/base/attributes.h"
#include "absl/status/status.h"

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
  using DrainCloseCb = std::function<absl::Status(std::chrono::milliseconds)>;

  virtual ~DrainDecision() = default;

  /**
   * @return TRUE if a connection should be drained and closed. It is up to individual network
   *         filters to determine when this should be called for the least impact possible.
   * @param direction supplies the direction for which the caller is checking drain close.
   */
  virtual bool drainClose(DrainDirection scope) const PURE;

  /**
   * @brief Register a callback to be called proactively when a drain decision enters into a
   *        'close' state.
   *        NOTE: this API is used in prorietary builds of Envoy and can not be decommissioned.
   *        TODO(yanavlasov): cleanup unused parts of this change without removing this API.
   *
   * @param cb Callback to be called once drain decision enters close state
   * @return handle to remove callback
   */
  ABSL_MUST_USE_RESULT
  virtual Common::CallbackHandlePtr addOnDrainCloseCb(DrainDirection scope,
                                                      DrainCloseCb cb) const PURE;
};

} // namespace Network
} // namespace Envoy
