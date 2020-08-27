#pragma once

#include "envoy/runtime/runtime.h"

#include "absl/strings/string_view.h"
#include "base_listener_impl.h"

namespace Envoy {
namespace Network {

/**
 * Listener accepting connection from thread local cluster.
 */
class InternalListenerImpl : public BaseListenerImpl {
public:
  InternalListenerImpl(Event::DispatcherImpl& dispatcher, const std::string& listener_id,
                       InternalListenerCallbacks& cb);
  void disable() override;
  void enable() override;

protected:
  void setupInternalListener(Event::DispatcherImpl& dispatcher,
                             const std::string& pipe_listener_id);

private:
  InteranalListenerCallbacks& cb_;
};

} // namespace Network
} // namespace Envoy
