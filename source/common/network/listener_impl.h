#pragma once

#include "envoy/runtime/runtime.h"

#include "absl/strings/string_view.h"
#include "base_listener_impl.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener for TCP.
 * TODO(conqerAtapple): Consider renaming the class to `TcpListenerImpl`.
 */
class ListenerImpl : public BaseListenerImpl {
public:
  ListenerImpl(Event::DispatcherImpl& dispatcher, SocketSharedPtr socket, ListenerCallbacks& cb,
               bool bind_to_port);

  void disable() override;
  void enable() override;

  static const absl::string_view GlobalMaxCxRuntimeKey;

protected:
  void setupServerSocket(Event::DispatcherImpl& dispatcher, Socket& socket);

  ListenerCallbacks& cb_;

private:
  static void listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* remote_addr,
                             int remote_addr_len, void* arg);
  static void errorCallback(evconnlistener* listener, void* context);

  // Returns true if global connection limit has been reached and the accepted socket should be
  // rejected/closed. If the accepted socket is to be admitted, false is returned.
  static bool rejectCxOverGlobalLimit();

  Event::Libevent::ListenerPtr listener_;
};

} // namespace Network
} // namespace Envoy
