#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace ConnectionPool {

/**
 * Controls the behavior of a canceled request.
 */
enum class CancelPolicy {
  // By default, canceled requests allow a pending connection to complete and become
  // available for a future request.
  Default,
  // When a request is canceled, closes a pending connection if there will still be sufficient
  // connections to serve pending requests. CloseExcess is largely useful for callers that never
  // re-use connections (e.g. by closing rather than releasing connections). Using CloseExcess in
  // this situation guarantees that no idle connections will be held open by the conn pool awaiting
  // a connection request.
  CloseExcess,
};

/**
 * Handle that allows a pending connection or stream request to be canceled before it is completed.
 */
class Cancellable {
public:
  virtual ~Cancellable() = default;

  /**
   * Cancel the pending connection or stream request.
   * @param cancel_policy a CancelPolicy that controls the behavior of this cancellation.
   */
  virtual void cancel(CancelPolicy cancel_policy) PURE;
};

enum class PoolFailureReason {
  // A resource overflowed and policy prevented a new connection from being created.
  Overflow,
  // A local connection failure took place while creating a new connection.
  LocalConnectionFailure,
  // A remote connection failure took place while creating a new connection.
  RemoteConnectionFailure,
  // A timeout occurred while creating a new connection.
  Timeout,
};

} // namespace ConnectionPool
} // namespace Envoy
