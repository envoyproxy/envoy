#pragma once

#include <atomic>

#include "common/buffer/buffer_impl.h"
#include "common/event/event_impl_base.h"

#include "base_listener_impl.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener for UDP.
 */
class UdpListenerImpl : public BaseListenerImpl, public Event::ImplBase {
public:
  UdpListenerImpl(const Event::DispatcherImpl& dispatcher, Socket& socket,
                  UdpListenerCallbacks& cb);

  virtual void disable() override;
  virtual void enable() override;

  struct ReceiveResult {
    Api::SysCallIntResult result_;
    Buffer::InstancePtr buffer_;
  };

  // Useful for testing/mocking.
  virtual ReceiveResult doRecvFrom(sockaddr_storage& peer_addr, socklen_t& addr_len);

protected:
  void handleWriteCallback(int fd);
  void handleReadCallback(int fd);

  UdpListenerCallbacks& cb_;

private:
  static void eventCallback(int fd, short flags, void* arg);
};

} // namespace Network
} // namespace Envoy
