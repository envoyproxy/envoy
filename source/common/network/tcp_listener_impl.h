#pragma once

#include "envoy/common/random_generator.h"
#include "envoy/runtime/runtime.h"

#include "source/common/common/interval_value.h"
#include "source/common/network/base_listener_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener for TCP.
 */
class TcpListenerImpl : public BaseListenerImpl {
public:
  TcpListenerImpl(Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                  Runtime::Loader& runtime, SocketSharedPtr socket, TcpListenerCallbacks& cb,
                  bool bind_to_port, bool ignore_global_conn_limit, bool bypass_overload_manager,
                  uint32_t max_connections_to_accept_per_socket_event,
                  Server::ThreadLocalOverloadStateOptRef overload_state);
  ~TcpListenerImpl() override {
    if (bind_to_port_) {
      socket_->ioHandle().resetFileEvents();
    }
  }
  void disable() override;
  void enable() override;
  void setRejectFraction(UnitFloat reject_fraction) override;
  void configureLoadShedPoints(Server::LoadShedPointProvider& load_shed_point_provider) override;
  bool shouldBypassOverloadManager() const override;

protected:
  TcpListenerCallbacks& cb_;

private:
  void onSocketEvent(short flags);

  // Returns true if global connection limit has been reached and the accepted socket should be
  // rejected/closed. If the accepted socket is to be admitted, false is returned.
  bool rejectCxOverGlobalLimit() const;

  Random::RandomGenerator& random_;
  Runtime::Loader& runtime_;
  bool bind_to_port_;
  UnitFloat reject_fraction_;
  const bool ignore_global_conn_limit_;
  const bool bypass_overload_manager_;
  const uint32_t max_connections_to_accept_per_socket_event_;
  Server::LoadShedPoint* listener_accept_{nullptr};
  Server::ThreadLocalOverloadStateOptRef overload_state_;
  const bool track_global_cx_limit_in_overload_manager_;
};

} // namespace Network
} // namespace Envoy
