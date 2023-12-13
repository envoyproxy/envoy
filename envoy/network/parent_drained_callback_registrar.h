#pragma once

#include <string>

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
   * @param address is the address of the listener. A std::string rather than a
   *                string_view or reference to avoid a copy if the string was
   *                constructed at the call-site.
   * @param callback the function to call when the listener matching address is
   *                 drained on the parent instance.
   */
  virtual void registerParentDrainedCallback(std::string address,
                                             absl::AnyInvocable<void()> callback) PURE;

protected:
  virtual ~ParentDrainedCallbackRegistrar() = default;
};

} // namespace Network
} // namespace Envoy
