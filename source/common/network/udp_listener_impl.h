#pragma once

#include <atomic>

#include "common/buffer/buffer_impl.h"
#include "common/event/event_impl_base.h"
#include "common/event/file_event_impl.h"

#include "base_listener_impl.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener for UDP.
 */
class UdpListenerImpl : public BaseListenerImpl {
public:
  UdpListenerImpl(Event::DispatcherImpl& dispatcher, Socket& socket, UdpListenerCallbacks& cb);

  ~UdpListenerImpl();

  virtual void disable() override;
  virtual void enable() override;

  struct ReceiveResult {
    Api::SysCallIntResult result_;
    Buffer::InstancePtr buffer_;
  };

  // Useful for testing/mocking.
  virtual ReceiveResult doRecvFrom(sockaddr_storage& peer_addr, socklen_t& addr_len);

protected:
  void handleWriteCallback();
  void handleReadCallback();

  UdpListenerCallbacks& cb_;

private:
  void onSocketEvent(short flags);
  Event::FileEventPtr file_event_;
};

} // namespace Network
} // namespace Envoy
