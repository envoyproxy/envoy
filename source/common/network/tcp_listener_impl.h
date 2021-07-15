#pragma once

#include "envoy/common/random_generator.h"
#include "envoy/runtime/runtime.h"

#include "source/common/common/interval_value.h"

#include "absl/strings/string_view.h"
#include "base_listener_impl.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener for TCP.
 */
class TcpListenerImpl : public BaseListenerImpl {
public:
  TcpListenerImpl(Event::DispatcherImpl& dispatcher, Random::RandomGenerator& random,
                  SocketSharedPtr socket, TcpListenerCallbacks& cb, bool bind_to_port);
  ~TcpListenerImpl() override {
    if (bind_to_port_) {
      socket_->ioHandle().resetFileEvents();
    }
  }
  void disable() override;
  void enable() override;
  void setRejectFraction(UnitFloat reject_fraction) override;

  static const absl::string_view GlobalMaxCxRuntimeKey;

protected:
  TcpListenerCallbacks& cb_;

private:
  void onSocketEvent(short flags);

  // Returns true if global connection limit has been reached and the accepted socket should be
  // rejected/closed. If the accepted socket is to be admitted, false is returned.
  static bool rejectCxOverGlobalLimit();

  Random::RandomGenerator& random_;
  bool bind_to_port_;
  UnitFloat reject_fraction_;
};

} // namespace Network
} // namespace Envoy
