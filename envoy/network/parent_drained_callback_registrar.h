#pragma once

#include "envoy/network/address.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Network {

/**
 * An interface through which a UDP listen socket, especially a QUIC socket, can
 * postpone reading during hot restart until the parent instance is drained.
 */
class ParentDrainedCallbackRegistrar {
public:
  /**
   * @param address is the address of the listener.
   * @param callback the function to call when the listener matching address is
   *                 drained on the parent instance.
   */
  virtual void registerParentDrainedCallback(const Address::InstanceConstSharedPtr& address,
                                             absl::AnyInvocable<void()> callback) PURE;

protected:
  virtual ~ParentDrainedCallbackRegistrar() = default;
};

} // namespace Network
} // namespace Envoy
